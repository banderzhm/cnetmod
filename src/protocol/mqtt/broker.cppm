/// cnetmod.protocol.mqtt:broker — MQTT Broker Core
/// TCP MQTT Broker, supports v3.1.1 and v5.0
/// channel-based dual-coroutine model for real-time delivery, trie subscription matching, security/ACL, TLS

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

namespace cnetmod::mqtt {

// =============================================================================
// Broker Configuration
// =============================================================================

export struct broker_options {
    std::uint16_t port                   = 1883;
    std::string   host                   = "127.0.0.1";
    std::uint16_t max_connections        = 10000;
    std::uint32_t default_session_expiry = 0;
    std::uint16_t max_keep_alive         = 600;
    std::size_t   max_inflight_messages  = 100;
    std::size_t   delivery_channel_size  = 1000;
    std::uint16_t topic_alias_maximum    = 0;

    std::uint16_t receive_maximum        = 65535;
    std::uint32_t maximum_packet_size    = 0;
    qos           maximum_qos            = qos::exactly_once;
    bool          retain_available       = true;
    bool          wildcard_sub_available = true;
    bool          sub_id_available       = true;
    bool          shared_sub_available   = true;

    std::uint16_t tls_port               = 0;
    std::string   tls_cert_file;
    std::string   tls_key_file;
    std::string   tls_ca_file;
};

// =============================================================================
// Connection State
// =============================================================================

namespace detail {

struct conn_state {
    socket           sock;
    io_context&      io;
    mqtt_parser      parser;
    session_state*   session    = nullptr;
    protocol_version version   = protocol_version::v3_1_1;
    bool             connected = false;
    std::uint16_t    keep_alive = 0;

    channel<publish_message> delivery_ch;
    topic_alias_recv alias_recv{0};
    std::size_t max_packet_size = 0;

    std::chrono::steady_clock::time_point last_packet_time
        = std::chrono::steady_clock::now();

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_stream> ssl;
#endif

    conn_state(socket s, io_context& ctx, std::size_t ch_cap)
        : sock(std::move(s)), io(ctx), delivery_ch(ch_cap) {}

    auto do_write(const std::string& data)
        -> task<std::expected<std::size_t, std::error_code>>;

    auto do_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;
};

} // namespace detail

// =============================================================================
// MQTT Broker
// =============================================================================

export class broker {
public:
    explicit broker(io_context& ctx) : ctx_(ctx) {}

    explicit broker(server_context& sctx)
        : ctx_(sctx.accept_io()), sctx_(&sctx) {}

    void set_options(broker_options opts) { opts_ = std::move(opts); }
    void set_security(security_config cfg) { security_ = std::move(cfg); }
    void set_auth_handler(broker_auth_handler h) { auth_handler_ = std::move(h); }

    [[nodiscard]] auto security() noexcept -> security_config& { return security_; }
    [[nodiscard]] auto sessions() noexcept -> session_store& { return sessions_; }
    [[nodiscard]] auto sessions() const noexcept -> const session_store& { return sessions_; }
    [[nodiscard]] auto retained() noexcept -> retained_store& { return retained_; }
    [[nodiscard]] auto retained() const noexcept -> const retained_store& { return retained_; }
    [[nodiscard]] auto subscriptions() noexcept -> subscription_map& { return sub_map_; }

    auto listen() -> std::expected<void, std::error_code> {
        return listen(opts_.host, opts_.port);
    }

    auto listen(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>;

#ifdef CNETMOD_HAS_SSL
    auto listen_tls(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>;

    auto listen_tls() -> std::expected<void, std::error_code> {
        if (opts_.tls_port == 0) return {};
        return listen_tls(opts_.host, opts_.tls_port);
    }
#endif

    auto run() -> task<void>;
    void stop();

private:
#ifdef CNETMOD_HAS_SSL
    auto run_tls_accept() -> task<void>;
#endif

    auto session_expiry_loop() -> task<void>;
    auto keep_alive_watchdog(std::shared_ptr<detail::conn_state> conn) -> task<void>;

    static auto read_frame(detail::conn_state& conn)
        -> task<std::expected<mqtt_frame, std::string>>;

    auto handle_connection(socket client, io_context& io, bool use_tls) -> task<void>;
    auto delivery_loop(std::shared_ptr<detail::conn_state> conn) -> task<void>;
    auto retry_loop(std::shared_ptr<detail::conn_state> conn) -> task<void>;

    auto handle_connect(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<bool>;
    auto handle_publish(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_puback(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_pubrec(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_pubrel(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_pubcomp(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_subscribe(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_unsubscribe(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_pingreq(std::shared_ptr<detail::conn_state> conn) -> task<void>;
    auto handle_disconnect(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_auth(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>;
    auto handle_unexpected_disconnect(std::shared_ptr<detail::conn_state> conn)
        -> task<void>;

    auto will_delay_task(io_context& io, std::string client_id,
                         will will_msg, std::uint32_t delay_sec) -> task<void>;
    auto publish_will(std::shared_ptr<detail::conn_state> conn) -> task<void>;
    auto cleanup_connection(std::shared_ptr<detail::conn_state> conn) -> task<void>;

    auto route_publish(const publish_message& msg, const std::string& sender_cid)
        -> task<void>;
    auto deliver_retained(std::shared_ptr<detail::conn_state> conn,
                          const subscribe_entry& entry,
                          const std::string& actual_filter) -> task<void>;
    auto deliver_inflight_resend(std::shared_ptr<detail::conn_state> conn) -> task<void>;
    auto deliver_offline_queue(std::shared_ptr<detail::conn_state> conn) -> task<void>;

    static auto send_disconnect_and_close(
        std::shared_ptr<detail::conn_state> conn,
        std::uint8_t reason_code
    ) -> task<void>;

    static auto generate_client_id() -> std::string;

    io_context&      ctx_;
    server_context*  sctx_     = nullptr;
    broker_options   opts_;
    session_store    sessions_;
    retained_store   retained_;
    subscription_map sub_map_;
    shared_target_store shared_store_;
    security_config  security_;
    broker_auth_handler auth_handler_;
    bool             running_  = false;
    std::unique_ptr<tcp::acceptor> acc_;

    async_shared_mutex channels_rw_;
    std::map<std::string, channel<publish_message>*> online_channels_;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context>   ssl_ctx_;
    std::unique_ptr<tcp::acceptor> tls_acc_;
#endif
};

} // namespace cnetmod::mqtt
