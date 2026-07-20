module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.raft:tcp_transport;

import std;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.file;
import cnetmod.core.ssl;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;
import :types;
import :node;
import :transport;
import :wire;

namespace cnetmod::raft {

export struct raft_tcp_peer {
  node_id id;
  endpoint address;
};

export struct raft_tcp_security_options {
  std::string shared_secret;
  bool require_auth_token = false;
  bool enable_tls = false;
  bool require_peer_certificate = false;
  std::map<node_id, std::string> peer_certificate_sha256;
#ifdef CNETMOD_HAS_SSL
  ssl_context *client_tls = nullptr;
  ssl_context *server_tls = nullptr;
#endif
};

export struct raft_snapshot_retention_options {
  std::size_t keep_last = 2;
  std::chrono::steady_clock::duration min_age{std::chrono::minutes{5}};
};

export struct raft_tcp_transport_options {
  std::uint32_t max_send_attempts = 3;
  std::chrono::milliseconds retry_backoff{20};
  std::filesystem::path snapshot_directory = "raft-snapshots";
  std::size_t snapshot_chunk_size = 1024 * 1024;
  std::chrono::milliseconds snapshot_session_timeout{std::chrono::minutes{5}};
  std::size_t max_outbound_queue = 1024;
  raft_tcp_security_options security;
  raft_snapshot_retention_options snapshot_retention;
};

export struct raft_peer_transport_metrics {
  node_id peer;
  bool connected = false;
  std::uint64_t queued_sends = 0;
  std::uint64_t send_successes = 0;
  std::uint64_t send_failures = 0;
  std::uint64_t reconnects = 0;
  std::uint64_t max_queue_depth = 0;
  std::uint64_t coalesced_sends = 0;
  std::chrono::steady_clock::duration last_queue_wait_latency{};
  std::chrono::steady_clock::duration last_send_latency{};
  std::chrono::steady_clock::time_point last_send_at{};
  std::chrono::steady_clock::time_point last_receive_at{};
  std::error_code last_error;
};

export class raft_tcp_transport final : public raft_transport {
public:
  raft_tcp_transport(io_context &ctx, node_id local_id,
                     raft_tcp_transport_options options = {});

  void set_node(raft_node &node) noexcept;

  void set_error_handler(std::function<void(node_id, std::error_code)> handler);

  void add_peer(raft_tcp_peer peer);

  void remove_peer(const node_id &peer);

  [[nodiscard]] auto peers() const -> std::vector<node_id>;

  [[nodiscard]] auto peer_metrics() const
      -> std::vector<raft_peer_transport_metrics>;

  [[nodiscard]] auto peer_metrics(const node_id &peer) const
      -> std::optional<raft_peer_transport_metrics>;

  void send_pre_vote(const node_id &peer,
                     const request_vote_request &request) override;

  void send_request_vote(const node_id &peer,
                         const request_vote_request &request) override;

  void
  send_request_vote_response(const node_id &peer,
                             const request_vote_response &response) override;

  void send_timeout_now(const node_id &peer,
                        const timeout_now_request &request) override;

  void send_append_entries(const node_id &peer,
                           const append_entries_request &request) override;

  void send_append_entries_response(
      const node_id &peer, const append_entries_response &response) override;

  void send_install_snapshot(const node_id &peer,
                             const install_snapshot_request &request) override;

  void send_install_snapshot_response(
      const node_id &peer, const install_snapshot_response &response) override;

  void broadcast_pre_vote(raft_node &node);

  void broadcast_request_vote(raft_node &node);

  void replicate_to_all(raft_node &node);

  auto transfer_leader(raft_node &node, const node_id &target)
      -> std::expected<void, raft_error>;

  auto serve(endpoint listen_endpoint)
      -> task<std::expected<void, std::error_code>>;

  auto serve(endpoint listen_endpoint, cancel_token &token)
      -> task<std::expected<void, std::error_code>>;

  void stop() noexcept;

  auto async_stop() -> task<void>;

  auto cleanup_snapshot_sessions() -> std::size_t;

  auto cleanup_snapshot_sessions(std::chrono::steady_clock::duration timeout)
      -> std::size_t;

  auto cleanup_snapshot_files()
      -> task<std::expected<std::size_t, std::error_code>>;

private:
  enum class snapshot_materialize_status {
    complete,
    pending,
    failed,
  };

  struct peer_connection {
    struct queued_message {
      endpoint ep;
      raft_rpc_message message;
      std::chrono::steady_clock::time_point enqueued_at;
    };

    socket sock;
    async_mutex write_lock;
    cancel_token write_token;
    std::deque<queued_message> outbound;
    bool writer_running = false;
  };

  struct inbound_socket_guard {
    raft_tcp_transport *owner = nullptr;
    socket *sock = nullptr;
    cancel_token *token = nullptr;

    inbound_socket_guard(raft_tcp_transport &transport, socket &inbound,
                         cancel_token &inbound_token) noexcept;

    ~inbound_socket_guard();

    inbound_socket_guard(const inbound_socket_guard &) = delete;
    auto operator=(const inbound_socket_guard &)
        -> inbound_socket_guard & = delete;
  };

  struct outbound_tls_socket_guard {
    raft_tcp_transport *owner = nullptr;
    socket *sock = nullptr;

    outbound_tls_socket_guard(raft_tcp_transport &transport,
                              socket &outbound) noexcept;

    ~outbound_tls_socket_guard();

    outbound_tls_socket_guard(const outbound_tls_socket_guard &) = delete;
    auto operator=(const outbound_tls_socket_guard &)
        -> outbound_tls_socket_guard & = delete;
  };

  struct snapshot_receive_state {
    std::uint64_t expected_offset = 0;
    std::filesystem::path temp_path;
    std::filesystem::path final_path;
    std::chrono::steady_clock::time_point last_activity =
        std::chrono::steady_clock::now();
  };

  struct snapshot_send_state {
    endpoint ep;
    install_snapshot_request request;
    std::chrono::steady_clock::time_point last_activity =
        std::chrono::steady_clock::now();
  };

  struct snapshot_file_candidate {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
  };

  void send(const node_id &peer, raft_rpc_message message);

  void notify_error(const node_id &peer, std::error_code ec);

  auto send_writer(node_id peer) -> task<void>;

  void spawn_tracked(task<void> operation);

  auto run_tracked(task<void> operation) -> task<void>;

  void coalesce_append_entries(const node_id &peer, peer_connection &conn,
                               peer_connection::queued_message &item);

  [[nodiscard]] static auto
  droppable_append_entries(const append_entries_request &request) -> bool;

  auto send_with_retry(const node_id &peer, endpoint ep,
                       const raft_rpc_message &message, cancel_token &token)
      -> task<std::expected<void, std::error_code>>;

  auto handle_connection(socket sock) -> task<void>;

  auto send_tls_once(const node_id &peer, const endpoint &ep,
                     const raft_rpc_message &message, cancel_token &token)
      -> task<std::expected<void, std::error_code>>;

#ifdef CNETMOD_HAS_SSL
  [[nodiscard]] auto verify_tls_peer(ssl_stream &stream,
                                     const node_id &peer) const
      -> std::expected<void, std::error_code>;
#endif

  auto send_pooled(const node_id &peer, const endpoint &ep,
                   const raft_rpc_message &message)
      -> task<std::expected<void, std::error_code>>;

  auto dispatch_message(raft_rpc_message &message) -> task<void>;

  auto materialize_snapshot(raft_rpc_message &message)
      -> task<snapshot_materialize_status>;

  auto send_snapshot_file(node_id peer, endpoint ep,
                          install_snapshot_request request,
                          std::uint64_t start_offset = 0) -> task<void>;

  auto send_snapshot_memory(node_id peer, endpoint ep,
                            install_snapshot_request request) -> task<void>;

  auto resume_snapshot_send(const node_id &peer,
                            const install_snapshot_response &response)
      -> task<void>;

  [[nodiscard]] auto request_chunk_size() const noexcept -> std::size_t;

  [[nodiscard]] auto
  default_snapshot_id(const install_snapshot_request &snapshot) const
      -> std::string;

  [[nodiscard]] auto
  snapshot_path(const node_id &from,
                const install_snapshot_request &snapshot) const
      -> std::filesystem::path;

  [[nodiscard]] static auto
  snapshot_session_key(const node_id &from,
                       const install_snapshot_request &snapshot) -> std::string;

  [[nodiscard]] static auto snapshot_send_key(const node_id &peer,
                                              std::string_view snapshot_id)
      -> std::string;

  [[nodiscard]] auto
  accepted_snapshot_offset(const node_id &from,
                           const install_snapshot_request &snapshot) const
      -> std::uint64_t;

  [[nodiscard]] static auto
  is_chunked_snapshot(const install_snapshot_request &snapshot) -> bool;

  static auto cleanup_snapshot_files_blocking(
      const std::filesystem::path &directory,
      const raft_snapshot_retention_options &retention)
      -> std::expected<std::size_t, std::error_code>;

  [[nodiscard]] static auto fnv1a64(std::string_view data,
                                    std::uint64_t seed = 1469598103934665603ull)
      -> std::uint64_t;

  [[nodiscard]] auto make_auth_token(const raft_rpc_message &message) const
      -> std::string;

  [[nodiscard]] auto authenticate(const raft_rpc_message &message) const
      -> bool;

  auto metrics_for(const node_id &peer) -> raft_peer_transport_metrics &;

  void record_send_success(const node_id &peer,
                           std::chrono::steady_clock::duration latency);

  void record_send_failure(const node_id &peer, std::error_code ec,
                           std::chrono::steady_clock::duration latency);

  io_context &ctx_;
  node_id local_id_;
  std::map<node_id, endpoint> peers_;
  std::map<node_id, peer_connection> connections_;
  std::set<socket *> inbound_sockets_;
  std::set<socket *> outbound_tls_sockets_;
  std::set<cancel_token *> inbound_tokens_;
  std::map<node_id, raft_peer_transport_metrics> peer_metrics_;
  std::map<std::string, snapshot_receive_state> snapshot_receives_;
  std::map<std::string, snapshot_send_state> snapshot_sends_;
  raft_tcp_transport_options options_;
  raft_node *node_ = nullptr;
  std::function<void(node_id, std::error_code)> on_error_;
  bool running_ = true;
  std::atomic<std::size_t> active_tasks_{0};
};
} // namespace cnetmod::raft
