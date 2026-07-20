/// cnetmod.protocol.mqtt:broker — MQTT Broker Core
/// TCP MQTT Broker, supports v3.1.1 and v5.0
/// channel-based dual-coroutine model for real-time delivery, trie subscription
/// matching, security/ACL, TLS

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:broker;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.log;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.channel;
import cnetmod.coro.mutex;
import cnetmod.coro.shared_mutex;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :types;
import :codec;
import :parser;
import :topic_filter;
import :session;
import :retained;
import :subscription_map;
import :shared_sub;
import :security;
import :topic_alias;
import :persistence;

namespace cnetmod::mqtt {

// =============================================================================
// Broker Configuration
// =============================================================================

export struct broker_options {
  std::uint16_t port = 1883;
  std::string host = "127.0.0.1";
  std::uint16_t max_connections = 10000;
  std::uint32_t default_session_expiry = 0;
  std::uint16_t max_keep_alive = 600;
  std::size_t max_inflight_messages = 100;
  std::size_t max_offline_queue = 1000;
  std::size_t delivery_channel_size = 1000;
  std::size_t write_channel_size = 4096;
  std::size_t write_batch_messages = 1;
  std::size_t write_batch_bytes = 8 * 1024;
  std::uint16_t topic_alias_maximum = 0;

  std::uint16_t receive_maximum = 65535;
  std::uint32_t maximum_packet_size = 0;
  qos maximum_qos = qos::exactly_once;
  bool retain_available = true;
  bool wildcard_sub_available = true;
  bool sub_id_available = true;
  bool shared_sub_available = true;

  std::uint16_t tls_port = 0;
  std::string tls_cert_file;
  std::string tls_key_file;
  std::string tls_ca_file;

  bool persistence_enabled = false;
  persistence_options persistence;
};

export struct broker_metrics_snapshot {
  std::uint64_t accepted_connections = 0;
  std::uint64_t active_connections = 0;
  std::uint64_t routed_messages = 0;
  std::uint64_t delivered_messages = 0;
  std::uint64_t offline_enqueued_messages = 0;
  std::uint64_t write_failures = 0;
  std::uint64_t protocol_errors = 0;
  std::uint64_t socket_writes = 0;
  std::uint64_t socket_write_bytes = 0;
  std::uint64_t batch_writes = 0;
  std::uint64_t batch_messages = 0;
};

export using publish_observer =
    std::function<void(const publish_message &msg, std::string_view client_id)>;

// =============================================================================
// Connection State
// =============================================================================

namespace detail {

enum class outbound_priority : std::uint8_t {
  normal,
  high,
};

struct outbound_packet {
  std::string data;
  outbound_priority priority = outbound_priority::normal;
  bool batchable = false;
  std::size_t logical_size = 0;
  std::shared_ptr<channel<std::expected<std::size_t, std::error_code>>>
      completion;
};

struct write_ticket {
  std::size_t logical_size = 0;
  std::shared_ptr<channel<std::expected<std::size_t, std::error_code>>>
      completion;
};

struct cross_delivery_item {
  publish_message msg;
};

struct conn_state {
  socket sock;
  io_context &io;
  mqtt_parser parser;
  session_state *session = nullptr;
  protocol_version version = protocol_version::v3_1_1;
  bool connected = false;
  bool cleanup_done = false;
  std::uint16_t keep_alive = 0;

  channel<publish_message> delivery_ch;
  channel<outbound_packet> write_ch;
  channel<std::monostate> write_wake_ch;
  async_mutex write_mtx;
  std::mutex cross_delivery_mtx;
  std::deque<cross_delivery_item> cross_delivery_pending;
  std::size_t cross_delivery_pending_limit = 0;
  bool cross_delivery_scheduled = false;
  topic_alias_recv alias_recv{0};
  std::size_t max_packet_size = 0;

  std::chrono::steady_clock::time_point last_packet_time =
      std::chrono::steady_clock::now();

#ifdef CNETMOD_HAS_SSL
  std::unique_ptr<ssl_stream> ssl;
#endif

  conn_state(socket s, io_context &ctx, std::size_t delivery_cap,
             std::size_t write_cap)
      : sock(std::move(s)), io(ctx), delivery_ch(delivery_cap),
        write_ch(write_cap), write_wake_ch(1),
        cross_delivery_pending_limit(delivery_cap) {}

  auto do_write(std::string data,
                outbound_priority priority = outbound_priority::normal,
                bool batchable = false)
      -> task<std::expected<std::size_t, std::error_code>>;

  auto submit_write(std::string data,
                    outbound_priority priority = outbound_priority::normal,
                    bool batchable = false)
      -> task<std::expected<write_ticket, std::error_code>>;

  auto
  submit_write_queued(std::string data,
                      outbound_priority priority = outbound_priority::normal,
                      bool batchable = false, std::size_t logical_messages = 1)
      -> task<std::expected<std::size_t, std::error_code>>;

  auto raw_write(const std::string &data)
      -> task<std::expected<std::size_t, std::error_code>>;

  auto do_read(mutable_buffer buf)
      -> task<std::expected<std::size_t, std::error_code>>;
};

} // namespace detail

struct online_delivery_target {
  std::weak_ptr<detail::conn_state> conn;
};

using route_cache_value = std::vector<std::pair<std::string, subscribe_entry>>;
struct string_view_hash {
  using is_transparent = void;

  auto operator()(std::string_view value) const noexcept -> std::size_t {
    return std::hash<std::string_view>{}(value);
  }

  auto operator()(const std::string &value) const noexcept -> std::size_t {
    return (*this)(std::string_view{value});
  }
};

struct string_view_equal {
  using is_transparent = void;

  auto operator()(std::string_view lhs, std::string_view rhs) const noexcept
      -> bool {
    return lhs == rhs;
  }

  auto operator()(const std::string &lhs, std::string_view rhs) const noexcept
      -> bool {
    return std::string_view{lhs} == rhs;
  }

  auto operator()(std::string_view lhs, const std::string &rhs) const noexcept
      -> bool {
    return lhs == std::string_view{rhs};
  }

  auto operator()(const std::string &lhs, const std::string &rhs) const noexcept
      -> bool {
    return lhs == rhs;
  }
};

using route_cache_map = std::unordered_map<std::string, route_cache_value,
                                           string_view_hash, string_view_equal>;

// =============================================================================
// MQTT Broker
// =============================================================================

export class broker {
public:
  explicit broker(io_context &ctx);

  explicit broker(server_context &sctx);

  void set_options(broker_options opts);
  void set_security(security_config cfg);
  void set_auth_handler(broker_auth_handler h);
  void set_publish_observer(publish_observer observer);

  [[nodiscard]] auto security() noexcept -> security_config &;
  [[nodiscard]] auto sessions() noexcept -> session_store &;
  [[nodiscard]] auto sessions() const noexcept -> const session_store &;
  [[nodiscard]] auto retained() noexcept -> retained_store &;
  [[nodiscard]] auto retained() const noexcept -> const retained_store &;
  [[nodiscard]] auto subscriptions() noexcept -> subscription_map &;
  [[nodiscard]] auto metrics() const noexcept -> broker_metrics_snapshot;

  auto listen() -> std::expected<void, std::error_code>;

  auto listen(std::string_view host, std::uint16_t port,
              socket_options opts = {.reuse_address = true,
                                     .non_blocking = true})
      -> std::expected<void, std::error_code>;

#ifdef CNETMOD_HAS_SSL
  auto listen_tls(std::string_view host, std::uint16_t port,
                  socket_options opts = {.reuse_address = true,
                                         .non_blocking = true})
      -> std::expected<void, std::error_code>;

  auto listen_tls() -> std::expected<void, std::error_code>;
#endif

  auto run() -> task<void>;
  void stop();

private:
#ifdef CNETMOD_HAS_SSL
  auto run_tls_accept() -> task<void>;
#endif

  void load_persistence_once();
  auto persistence_flush_loop() -> task<void>;
  void flush_persistence_blocking();

  auto session_expiry_loop() -> task<void>;
  auto keep_alive_watchdog(std::shared_ptr<detail::conn_state> conn)
      -> task<void>;

  static auto read_frame(detail::conn_state &conn)
      -> task<std::expected<mqtt_frame, std::string>>;

  auto handle_connection(socket client, io_context &io, bool use_tls)
      -> task<void>;
  auto writer_loop(std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto delivery_loop(std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto retry_loop(std::shared_ptr<detail::conn_state> conn) -> task<void>;

  auto handle_connect(const mqtt_frame &frame,
                      std::shared_ptr<detail::conn_state> conn) -> task<bool>;
  auto handle_publish(const mqtt_frame &frame,
                      std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_puback(const mqtt_frame &frame,
                     std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_pubrec(const mqtt_frame &frame,
                     std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_pubrel(const mqtt_frame &frame,
                     std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_pubcomp(const mqtt_frame &frame,
                      std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_subscribe(const mqtt_frame &frame,
                        std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_unsubscribe(const mqtt_frame &frame,
                          std::shared_ptr<detail::conn_state> conn)
      -> task<void>;
  auto handle_pingreq(std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_disconnect(const mqtt_frame &frame,
                         std::shared_ptr<detail::conn_state> conn)
      -> task<void>;
  auto handle_auth(const mqtt_frame &frame,
                   std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto handle_unexpected_disconnect(std::shared_ptr<detail::conn_state> conn)
      -> task<void>;

  auto will_delay_task(io_context &io, std::string client_id, will will_msg,
                       std::uint32_t delay_sec) -> task<void>;
  auto publish_will(std::shared_ptr<detail::conn_state> conn) -> task<void>;
  auto cleanup_connection(std::shared_ptr<detail::conn_state> conn)
      -> task<void>;

  auto route_publish(io_context &origin_io, const publish_message &msg,
                     const std::string &sender_cid) -> task<void>;
  auto deliver_retained(std::shared_ptr<detail::conn_state> conn,
                        const subscribe_entry &entry,
                        const std::string &actual_filter) -> task<void>;
  auto deliver_inflight_resend(std::shared_ptr<detail::conn_state> conn)
      -> task<void>;
  auto deliver_offline_queue(std::shared_ptr<detail::conn_state> conn)
      -> task<void>;

  static auto
  send_disconnect_and_close(std::shared_ptr<detail::conn_state> conn,
                            std::uint8_t reason_code) -> task<void>;

  static auto generate_client_id() -> std::string;
  void invalidate_route_cache() noexcept;

  io_context &ctx_;
  server_context *sctx_ = nullptr;
  broker_options opts_;
  session_store sessions_;
  retained_store retained_;
  subscription_map sub_map_;
  shared_target_store shared_store_;
  security_config security_;
  broker_auth_handler auth_handler_;
  publish_observer publish_observer_;
  bool running_ = false;
  std::unique_ptr<tcp::acceptor> acc_;
  std::atomic_uint64_t accepted_connections_{0};
  std::atomic_uint64_t active_connections_{0};
  std::atomic_uint64_t routed_messages_{0};
  std::atomic_uint64_t delivered_messages_{0};
  std::atomic_uint64_t offline_enqueued_messages_{0};
  std::atomic_uint64_t write_failures_{0};
  std::atomic_uint64_t protocol_errors_{0};
  std::atomic_uint64_t socket_writes_{0};
  std::atomic_uint64_t socket_write_bytes_{0};
  std::atomic_uint64_t batch_writes_{0};
  std::atomic_uint64_t batch_messages_{0};

  async_mutex state_mtx_;
  bool persistence_loaded_ = false;
  std::shared_ptr<const route_cache_map> route_cache_ =
      std::make_shared<route_cache_map>();
  std::atomic_bool shared_subscriptions_present_{false};
  async_shared_mutex channels_rw_;
  std::unordered_map<std::string, online_delivery_target> online_channels_;

#ifdef CNETMOD_HAS_SSL
  std::unique_ptr<ssl_context> ssl_ctx_;
  std::unique_ptr<tcp::acceptor> tls_acc_;
#endif
};

} // namespace cnetmod::mqtt
