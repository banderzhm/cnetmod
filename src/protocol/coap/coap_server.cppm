/// cnetmod.protocol.coap:server - UDP CoAP server and resource router
/// interface.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:server;

import std;
import :types;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::coap {

export using request_handler =
    std::function<task<message>(const inbound_request &, const endpoint &)>;

export using etag_provider =
    std::function<std::optional<std::vector<std::byte>>(std::string_view path)>;

export struct observe_subscription {
  endpoint peer;
  std::vector<std::byte> token;
  std::string path;
  std::chrono::steady_clock::time_point updated_at{};
};

export struct resource_description {
  std::string path;
  std::string rt;
  std::string if_;
  bool observable = false;
};

export class resource_router {
public:
  void add(method m, std::string path, request_handler handler);

  [[nodiscard]] auto dispatch(const inbound_request &req,
                              const endpoint &peer) const -> task<message>;

private:
  struct route_key {
    method m{};
    std::string path;

    auto operator==(const route_key &rhs) const noexcept -> bool;
  };

  struct route_hash {
    auto operator()(const route_key &key) const noexcept -> std::size_t;
  };

  static auto normalize_path(std::string path) -> std::string;
  [[nodiscard]] auto has_path(std::string_view path) const -> bool;

  std::unordered_map<route_key, request_handler, route_hash> routes_;
};

export class udp_server {
public:
  explicit udp_server(io_context &ctx, server_config cfg = {});

  udp_server(const udp_server &) = delete;
  auto operator=(const udp_server &) -> udp_server & = delete;

  auto listen(std::string_view host, std::uint16_t port = default_port,
              socket_options opts = {.reuse_address = true,
                                     .non_blocking = true})
      -> std::expected<void, std::error_code>;

  void set_handler(request_handler handler);
  void set_etag_provider(etag_provider provider);
  void route(method m, std::string path, request_handler handler);
  void register_resource(resource_description desc);
  [[nodiscard]] auto
  join_multicast_group(const ip_address &group,
                       std::optional<ip_address> local_address = std::nullopt,
                       unsigned int interface_index = 0)
      -> std::expected<void, std::error_code>;
  [[nodiscard]] auto
  leave_multicast_group(const ip_address &group,
                        std::optional<ip_address> local_address = std::nullopt,
                        unsigned int interface_index = 0)
      -> std::expected<void, std::error_code>;

  auto run() -> task<void>;
  void stop() noexcept;

  [[nodiscard]] auto is_running() const noexcept -> bool;

  auto notify_observers(std::string path, message notification)
      -> task<std::expected<std::size_t, std::error_code>>;

private:
  enum class block1_result {
    none,
    continue_,
    complete,
    incomplete,
    too_large,
  };

  struct block1_key {
    std::string peer;
    std::vector<std::byte> token;
    std::string path;

    auto operator==(const block1_key &rhs) const noexcept -> bool;
  };

  struct block1_hash {
    auto operator()(const block1_key &key) const noexcept -> std::size_t;
  };

  struct block1_transfer {
    std::vector<std::byte> payload;
    std::chrono::steady_clock::time_point updated_at{};
  };

  struct proxy_target {
    std::string host;
    std::uint16_t port = default_port;
    std::string path;
    std::string query;
  };

  struct observer_key {
    std::string peer;
    std::vector<std::byte> token;
    std::string path;

    auto operator==(const observer_key &rhs) const noexcept -> bool;
  };

  struct observer_hash {
    auto operator()(const observer_key &key) const noexcept -> std::size_t;
  };

  struct observe_delivery {
    endpoint peer;
    std::vector<std::byte> token;
    std::string path;
    std::chrono::steady_clock::time_point sent_at{};
  };

  static auto normalize_path(std::string path) -> std::string;
  static void normalize_response(const message &req, message &resp);
  static auto is_known_option(std::uint16_t number) noexcept -> bool;
  static auto has_unsupported_critical_option(const message &msg) noexcept
      -> bool;
  static auto response_content_format(const message &resp)
      -> std::optional<std::uint32_t>;
  static auto request_accepts_response(const inbound_request &req,
                                       const message &resp) -> bool;
  auto check_preconditions(const inbound_request &req) const
      -> std::optional<message>;

  auto make_discovery_response(const inbound_request &req)
      -> std::optional<message>;
  auto parse_proxy_uri(std::string_view uri) -> std::optional<proxy_target>;
  auto proxy_request(const inbound_request &req)
      -> task<std::optional<message>>;

  auto apply_block1(const endpoint &peer, inbound_request &req)
      -> block1_result;
  void apply_block2(const inbound_request &req, message &resp);

  void apply_observe_state(const inbound_request &req, const endpoint &peer,
                           message &resp);
  void add_observer(const endpoint &peer, const std::vector<std::byte> &token,
                    std::string path);
  void remove_observer(const endpoint &peer,
                       const std::vector<std::byte> &token, std::string path);
  auto collect_observers(std::string path) -> std::vector<observe_subscription>;
  void track_observe_delivery(std::uint16_t message_id,
                              const observe_subscription &sub);
  auto handle_observe_control(const endpoint &peer, const message &msg) -> bool;
  static auto observe_delivery_key(const endpoint &peer,
                                   std::uint16_t message_id) -> std::string;

  auto next_message_id() -> std::uint16_t;
  auto next_observe_sequence() -> std::uint32_t;
  auto send_reset(const endpoint &peer, std::uint16_t message_id)
      -> task<std::expected<void, std::error_code>>;

  io_context &ctx_;
  server_config cfg_;
  socket sock_;
  bool running_ = false;
  request_handler handler_;
  etag_provider etag_provider_;
  resource_router router_;
  std::atomic<std::uint16_t> message_id_{1};
  std::atomic<std::uint32_t> observe_sequence_{1};
  std::mutex observers_mutex_;
  std::unordered_map<observer_key, observe_subscription, observer_hash>
      observers_;
  std::unordered_map<std::string, observe_delivery> pending_observe_;
  std::mutex resources_mutex_;
  std::vector<resource_description> resources_;
  std::mutex block1_mutex_;
  std::unordered_map<block1_key, block1_transfer, block1_hash> block1_;
};

} // namespace cnetmod::coap
