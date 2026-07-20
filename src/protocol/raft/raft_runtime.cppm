export module cnetmod.protocol.raft:runtime;

import std;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import :types;
import :node;
import :tcp_transport;

namespace cnetmod::raft {
export struct raft_runtime_options {
  bool start_tcp_server = true;
  bool auto_election = true;
  bool auto_heartbeat = true;
  bool auto_snapshot = false;
  raft_snapshot_policy snapshot_policy;
};

export class raft_node_runtime {
public:
  raft_node_runtime(io_context &ctx, raft_node &node,
                    raft_tcp_transport &transport, endpoint listen_endpoint,
                    raft_options options,
                    raft_runtime_options runtime_options = {});

  raft_node_runtime(const raft_node_runtime &) = delete;
  auto operator=(const raft_node_runtime &) -> raft_node_runtime & = delete;

  void start();
  void stop() noexcept;
  auto async_stop() -> task<void>;
  [[nodiscard]] auto running() const noexcept -> bool;
  void tick_now();
  auto transfer_leader(const node_id &target)
      -> std::expected<void, raft_error>;
  auto maybe_snapshot_now()
      -> std::expected<std::optional<snapshot_metadata>, raft_error>;
  auto async_read_index(read_index_request request)
      -> task<std::expected<read_index_response, raft_error>>;

private:
  auto serve_loop() -> task<void>;
  auto election_loop() -> task<void>;
  auto heartbeat_loop() -> task<void>;
  void spawn_loop(task<void> loop);
  auto run_loop(task<void> loop) -> task<void>;
  auto randomized_election_timeout() -> std::chrono::milliseconds;

  io_context &ctx_;
  raft_node &node_;
  raft_tcp_transport &transport_;
  endpoint listen_endpoint_;
  raft_options options_;
  raft_runtime_options runtime_options_;
  std::atomic<bool> running_{false};
  std::atomic<std::size_t> active_loops_{0};
  cancel_token accept_token_;
  cancel_token election_token_;
  cancel_token heartbeat_token_;
  std::mt19937_64 rng_{std::random_device{}()};
};
} // namespace cnetmod::raft
