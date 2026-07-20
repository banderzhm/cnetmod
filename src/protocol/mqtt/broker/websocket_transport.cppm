/// MQTT over WebSocket broker public API.
module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:ws_transport;

import std;
import cnetmod.core.error;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;
import :types;
import :session;
import :retained;
import :subscription_map;
import :shared_sub;
import :security;

export namespace cnetmod::mqtt {
struct ws_broker_options {
  std::uint16_t port = 8083;
  std::string host = "0.0.0.0";
  std::string path = "/mqtt";
  std::uint16_t max_connections = 10000;
  std::uint32_t default_session_expiry = 0;
  std::uint16_t max_keep_alive = 600;
  std::size_t delivery_channel_size = 1000;
  std::uint16_t topic_alias_maximum = 0;
  std::uint16_t receive_maximum = 65535;
  std::uint32_t maximum_packet_size = 0;
  qos maximum_qos = qos::exactly_once;
  bool retain_available = true;
  bool wildcard_sub_available = true;
  bool sub_id_available = true;
  bool shared_sub_available = true;
};

class ws_broker_impl;

class ws_broker {
public:
  explicit ws_broker(io_context &ctx);
  explicit ws_broker(server_context &sctx);
  ~ws_broker();
  ws_broker(const ws_broker &) = delete;
  auto operator=(const ws_broker &) -> ws_broker & = delete;
  void set_options(ws_broker_options opts);
  void set_security(security_config cfg);
  void set_auth_handler(broker_auth_handler h);
  [[nodiscard]] auto security() noexcept -> security_config &;
  [[nodiscard]] auto sessions() noexcept -> session_store &;
  [[nodiscard]] auto sessions() const noexcept -> const session_store &;
  [[nodiscard]] auto retained() noexcept -> retained_store &;
  [[nodiscard]] auto retained() const noexcept -> const retained_store &;
  [[nodiscard]] auto subscriptions() noexcept -> subscription_map &;
  auto listen() -> std::expected<void, std::error_code>;
  auto listen(std::string_view host, std::uint16_t port,
              socket_options opts = {.reuse_address = true})
      -> std::expected<void, std::error_code>;
  auto run() -> task<void>;
  void stop();

private:
  std::unique_ptr<ws_broker_impl> impl_;
};
} // namespace cnetmod::mqtt
