/// cnetmod.protocol.mqtt:sync_client — MQTT synchronous client
/// Wraps async client, provides blocking API

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:sync_client;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import :types;
import :client;

namespace cnetmod::mqtt {

// =============================================================================
// MQTT synchronous client
// =============================================================================

/// Synchronous client: wraps async client, uses io_context to drive coroutines
/// to completion Suitable for simple scripts, tests, and scenarios that don't
/// need coroutines
export class sync_client {
public:
  explicit sync_client();

  ~sync_client() = default;

  // Non-copyable
  sync_client(const sync_client &) = delete;
  auto operator=(const sync_client &) -> sync_client & = delete;

  /// Synchronously connect to MQTT Broker
  auto connect_sync(connect_options opts = {})
      -> std::expected<void, std::string>;

  /// Synchronously publish message
  auto publish_sync(std::string_view topic, std::string_view payload,
                    qos q = qos::at_most_once, bool retain = false,
                    const properties &props = {})
      -> std::expected<void, std::string>;

  /// Synchronously subscribe
  auto subscribe_sync(std::string topic_filter, qos max_qos = qos::at_most_once,
                      const properties &props = {})
      -> std::expected<std::vector<std::uint8_t>, std::string>;

  /// Synchronously unsubscribe
  auto unsubscribe_sync(std::vector<std::string> topic_filters,
                        const properties &props = {})
      -> std::expected<void, std::string>;

  /// Synchronously disconnect
  auto disconnect_sync(std::uint8_t reason_code = 0,
                       const properties &props = {})
      -> std::expected<void, std::string>;

  /// Register message arrival callback
  void on_message(message_callback cb);

  /// Register disconnection callback
  void on_disconnect(disconnect_callback cb);

  /// Drive event loop once (process received messages, etc.)
  void poll();

  /// Drive event loop until no pending events
  void poll_all();

  /// Get underlying async client
  [[nodiscard]] auto async_client() noexcept -> client &;
  [[nodiscard]] auto async_client() const noexcept -> const client &;

  /// Get io_context
  [[nodiscard]] auto context() noexcept -> io_context &;

  /// Whether connected
  [[nodiscard]] auto is_connected() const noexcept -> bool;

private:
  /// Drive event loop until condition is met
  void run_until(bool &done);

  std::unique_ptr<io_context> ctx_;
  client client_;
};

} // namespace cnetmod::mqtt
