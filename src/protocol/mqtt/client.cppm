/// cnetmod.protocol.mqtt:client — MQTT Async Client
/// Full coroutine-based client supporting MQTT v3.1.1 and v5.0

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.channel;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.core.log;
import cnetmod.core.dns;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :types;
import :codec;
import :parser;
import :topic_alias;
import :session;

namespace cnetmod::mqtt {

// =============================================================================
// Message Callback Types
// =============================================================================

/// Callback invoked when a PUBLISH message is received
export using message_callback = std::function<void(const publish_message&)>;

/// Callback invoked when connection is lost
export using disconnect_callback = std::function<void(std::string reason)>;

/// v5 AUTH callback (Enhanced Authentication)
/// Parameters: reason_code, properties  Returns: whether to auto-send AUTH response
export using auth_callback = std::function<
    std::optional<std::pair<std::uint8_t, properties>>(
        std::uint8_t reason_code, const properties& props)>;

/// Auto-reconnect configuration
export struct reconnect_options {
    bool                     enabled              = false;
    std::uint32_t            max_retries          = 0;     // 0=unlimited retries
    std::chrono::milliseconds initial_delay       = std::chrono::seconds(1);
    std::chrono::milliseconds max_delay           = std::chrono::seconds(60);
    double                   backoff_multiplier   = 2.0;
    bool                     restore_subscriptions = true;
};

// =============================================================================
// MQTT Async Client
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) noexcept : ctx_(ctx) {}

    ~client() { close(); }

    // Non-copyable
    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;

    // ----- Connection / Disconnection -----

    /// Connect to MQTT Broker
    auto connect(connect_options opts = {}) -> task<std::expected<void, std::string>> {
        version_ = opts.version;
        keep_alive_sec_ = opts.keep_alive_sec;

        // TCP connection: Try IP literal first, fallback to async DNS resolution
        auto addr_r = ip_address::from_string(opts.host);
        if (!addr_r) {
            auto dns_r = co_await async_resolve(ctx_, opts.host,
                std::to_string(opts.port));
            if (!dns_r || dns_r->empty()) {
                logger::error("mqtt client connect: cannot resolve host {}", opts.host);
                co_return std::unexpected(std::string("cannot resolve host: ") + opts.host);
            }
            addr_r = ip_address::from_string(dns_r->front());
            if (!addr_r) {
                co_return std::unexpected(std::string("resolved address invalid: ") + dns_r->front());
            }
        }

        auto family = addr_r->is_v4() ? address_family::ipv4 : address_family::ipv6;
        auto sock_r = socket::create(family, socket_type::stream);
        if (!sock_r)
            co_return std::unexpected(std::string("socket create failed"));
        sock_ = std::move(*sock_r);

        // TCP connection timeout
        if (opts.connect_timeout.count() > 0) {
            cancel_token conn_token;
            auto cr = co_await with_timeout(ctx_, opts.connect_timeout,
                async_connect(ctx_, sock_, endpoint{*addr_r, opts.port}, conn_token),
                conn_token);
            if (!cr) {
                sock_.close();
                auto ec = cr.error();
                if (ec == std::errc::operation_canceled)
                    co_return std::unexpected(std::string("connect timeout"));
                co_return std::unexpected(std::string("connect failed: ") + ec.message());
            }
        } else {
            auto cr = co_await async_connect(ctx_, sock_, endpoint{*addr_r, opts.port});
            if (!cr) {
                sock_.close();
                co_return std::unexpected(std::string("connect failed: ") + cr.error().message());
            }
        }

        // TLS
#ifdef CNETMOD_HAS_SSL
        if (opts.tls) {
            auto ssl_ctx_r = ssl_context::client();
            if (!ssl_ctx_r) {
                sock_.close();
                co_return std::unexpected("ssl context: " + ssl_ctx_r.error().message());
            }
            ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
            ssl_ctx_->set_verify_peer(opts.tls_verify);
            if (!opts.tls_ca_file.empty()) {
                auto r = ssl_ctx_->load_ca_file(opts.tls_ca_file);
                if (!r) { sock_.close(); co_return std::unexpected("ssl ca: " + r.error().message()); }
            } else if (opts.tls_verify) {
                (void)ssl_ctx_->set_default_ca();
            }
            if (!opts.tls_cert_file.empty()) {
                auto r = ssl_ctx_->load_cert_file(opts.tls_cert_file);
                if (!r) { sock_.close(); co_return std::unexpected("ssl cert: " + r.error().message()); }
            }
            if (!opts.tls_key_file.empty()) {
                auto r = ssl_ctx_->load_key_file(opts.tls_key_file);
                if (!r) { sock_.close(); co_return std::unexpected("ssl key: " + r.error().message()); }
            }
            ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
            ssl_->set_connect_state();
            ssl_->set_hostname(opts.tls_sni.empty() ? opts.host : opts.tls_sni);
            auto hs = co_await ssl_->async_handshake();
            if (!hs) { sock_.close(); co_return std::unexpected("ssl handshake: " + hs.error().message()); }
        }
#else
        if (opts.tls) {
            sock_.close();
            co_return std::unexpected(std::string("SSL not available"));
        }
#endif

        // Send CONNECT packet
        auto connect_pkt = encode_connect(opts);
        auto wr = co_await do_write(const_buffer{connect_pkt.data(), connect_pkt.size()});
        if (!wr) {
            sock_.close();
            co_return std::unexpected(std::string("send CONNECT failed: ") + wr.error().message());
        }

        // Wait for CONNACK
        auto frame_r = co_await read_frame();
        if (!frame_r)
            co_return std::unexpected(std::string("read CONNACK failed: ") + frame_r.error());

        auto& frame = *frame_r;
        if (frame.type != control_packet_type::connack)
            co_return std::unexpected(std::string("expected CONNACK, got ") +
                                      std::string(to_string(frame.type)));

        auto connack_r = decode_connack(frame.payload, version_);
        if (!connack_r)
            co_return std::unexpected(std::string("decode CONNACK: ") + connack_r.error());

        auto& connack = *connack_r;
        if (version_ == protocol_version::v5) {
            if (v5::is_error(static_cast<v5::connect_reason_code>(connack.v5_reason))) {
                sock_.close();
                co_return std::unexpected(
                    std::string("CONNACK v5 error: reason_code=") +
                    std::to_string(connack.v5_reason));
            }
            connack_props_ = std::move(connack.props);
        } else {
            if (connack.return_code != connect_return_code::accepted) {
                sock_.close();
                co_return std::unexpected(
                    std::string("CONNACK refused: ") +
                    std::string(to_string(connack.return_code)));
            }
        }

        session_present_ = connack.session_present;
        connected_ = true;

        // v5: Extract key properties from CONNACK
        if (version_ == protocol_version::v5) {
            for (auto& p : connack_props_) {
                if (p.id == property_id::topic_alias_maximum)
                    if (auto* v = std::get_if<std::uint16_t>(&p.value))
                        alias_send_.set_max(*v);
                if (p.id == property_id::maximum_packet_size)
                    if (auto* v = std::get_if<std::uint32_t>(&p.value))
                        max_packet_size_ = static_cast<std::size_t>(*v);
                if (p.id == property_id::receive_maximum)
                    if (auto* v = std::get_if<std::uint16_t>(&p.value))
                        receive_maximum_ = *v;
            }
        }

        logger::info("mqtt client connected to {}:{} version={} session_present={}",
            opts.host, opts.port, to_string(version_), session_present_);

        // Save connection parameters (for reconnection)
        last_connect_opts_ = opts;

        // Session resumption: Resend inflight messages
        if (session_present_)
            co_await resend_inflight();

        // Start internal read loop
        spawn(ctx_, read_loop());

        // Start keep-alive timer
        if (keep_alive_sec_ > 0)
            spawn(ctx_, keep_alive_loop());

        // Start QoS retransmission timer
        spawn(ctx_, retry_loop());

        co_return std::expected<void, std::string>{};
    }

    /// Check if connected
    [[nodiscard]] auto is_connected() const noexcept -> bool { return connected_; }

    /// Get session present flag
    [[nodiscard]] auto session_present() const noexcept -> bool { return session_present_; }

    /// Get CONNACK properties (v5)
    [[nodiscard]] auto connack_properties() const noexcept -> const properties& { return connack_props_; }

    /// Close connection
    void close() noexcept {
        connected_ = false;
#ifdef CNETMOD_HAS_SSL
        ssl_.reset();
        ssl_ctx_.reset();
#endif
        sock_.close();
    }

    // ----- Publish -----

    /// Publish message
    auto publish(
        std::string_view topic,
        std::string_view payload,
        qos q = qos::at_most_once,
        bool retain = false,
        const properties& props = {}
    ) -> task<std::expected<void, std::string>>
    {
        if (!connected_) {
            logger::warn("mqtt client publish: not connected");
            co_return std::unexpected(std::string("not connected"));
        }
        logger::debug("mqtt client publish topic={} qos={} retain={}",
            topic, to_string(q), retain);

        // Receive Maximum flow control: QoS 1/2 wait for inflight quota
        if (q != qos::at_most_once) {
            for (int wait_i = 0; wait_i < 3000; ++wait_i) {
                if (inflight_out_.size() < static_cast<std::size_t>(receive_maximum_))
                    break;
                if (!connected_)
                    co_return std::unexpected(std::string("disconnected while waiting for inflight quota"));
                co_await async_sleep(ctx_, std::chrono::milliseconds(10));
            }
        }

        std::uint16_t pid = 0;
        if (q != qos::at_most_once) {
            pid = alloc_packet_id();
        }

        // v5 Topic Alias
        properties pub_props = props;
        std::string_view send_topic = topic;
        if (version_ == protocol_version::v5 && alias_send_.enabled()) {
            auto [alias, is_new] = alias_send_.allocate(topic);
            if (alias != 0) {
                pub_props.push_back({property_id::topic_alias, alias});
                if (!is_new) send_topic = {};  // Known alias, topic is empty
            }
        }

        auto pkt = encode_publish(send_topic, payload, q, retain, false, pid,
                                  version_, pub_props);

        // Maximum Packet Size check
        if (max_packet_size_ > 0 && pkt.size() > max_packet_size_)
            co_return std::unexpected(std::string("packet exceeds server maximum_packet_size"));

        auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
        if (!wr)
            co_return std::unexpected(wr.error().message());

        // QoS 0: No need to wait for ACK
        if (q == qos::at_most_once)
            co_return std::expected<void, std::string>{};

        // Record inflight
        {
            inflight_message im;
            im.packet_id    = pid;
            im.msg.topic    = std::string(topic);
            im.msg.payload  = std::string(payload);
            im.msg.qos_value = q;
            im.msg.retain   = retain;
            im.msg.props    = props;
            im.expected_ack = (q == qos::at_least_once)
                ? control_packet_type::puback : control_packet_type::pubrec;
            im.send_time    = std::chrono::steady_clock::now();
            inflight_out_.push_back(std::move(im));
        }

        // QoS 1: Wait for PUBACK
        if (q == qos::at_least_once) {
            auto ack_r = co_await wait_for_ack(pid);
            if (!ack_r) co_return std::unexpected(ack_r.error());
            co_return std::expected<void, std::string>{};
        }

        // QoS 2: Wait for PUBREC → Send PUBREL → Wait for PUBCOMP
        auto rec_r = co_await wait_for_ack(pid);
        if (!rec_r) co_return std::unexpected(rec_r.error());

        // Update inflight state to wait for PUBCOMP
        for (auto& im : inflight_out_)
            if (im.packet_id == pid) {
                im.expected_ack = control_packet_type::pubcomp;
                break;
            }

        // Send PUBREL
        auto pubrel_pkt = encode_pubrel(pid, version_);
        auto wr2 = co_await do_write(const_buffer{pubrel_pkt.data(), pubrel_pkt.size()});
        if (!wr2) co_return std::unexpected(wr2.error().message());

        // Wait for PUBCOMP
        auto comp_r = co_await wait_for_ack(pid);
        if (!comp_r) co_return std::unexpected(comp_r.error());

        co_return std::expected<void, std::string>{};
    }

    // ----- Subscribe -----

    /// Subscribe to topics
    auto subscribe(
        std::vector<subscribe_entry> entries,
        const properties& props = {}
    ) -> task<std::expected<std::vector<std::uint8_t>, std::string>>
    {
        if (!connected_)
            co_return std::unexpected(std::string("not connected"));

        auto pid = alloc_packet_id();
        auto pkt = encode_subscribe(pid, entries, version_, props);
        auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
        if (!wr)
            co_return std::unexpected(wr.error().message());

        // Wait for SUBACK
        auto ack_r = co_await wait_for_suback(pid);
        if (!ack_r) co_return std::unexpected(ack_r.error());

        // Save subscriptions (for auto-reconnect restoration)
        for (auto& e : entries)
            save_subscription(e);

        co_return ack_r->return_codes;
    }

    /// Convenience: Single topic subscription
    auto subscribe(
        std::string topic_filter,
        qos max_qos = qos::at_most_once,
        const properties& props = {}
    ) -> task<std::expected<std::vector<std::uint8_t>, std::string>>
    {
        std::vector<subscribe_entry> entries;
        entries.push_back({std::move(topic_filter), max_qos});
        co_return co_await subscribe(std::move(entries), props);
    }

    // ----- Unsubscribe -----

    /// Unsubscribe from topics
    auto unsubscribe(
        std::vector<std::string> topic_filters,
        const properties& props = {}
    ) -> task<std::expected<void, std::string>>
    {
        if (!connected_)
            co_return std::unexpected(std::string("not connected"));

        auto pid = alloc_packet_id();
        auto pkt = encode_unsubscribe(pid, topic_filters, version_, props);
        auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
        if (!wr)
            co_return std::unexpected(wr.error().message());

        // Wait for UNSUBACK
        auto ack_r = co_await wait_for_ack(pid);
        if (!ack_r) co_return std::unexpected(ack_r.error());

        // Remove saved subscriptions
        for (auto& tf : topic_filters)
            remove_saved_subscription(tf);

        co_return std::expected<void, std::string>{};
    }

    // ----- Disconnect -----

    /// Graceful disconnect
    auto disconnect(
        std::uint8_t reason_code = 0,
        const properties& props = {}
    ) -> task<std::expected<void, std::string>>
    {
        if (!connected_)
            co_return std::expected<void, std::string>{};

        // Disable auto-reconnect on intentional disconnect
        reconnecting_ = false;

        logger::info("mqtt client disconnect reason_code={}", reason_code);
        auto pkt = encode_disconnect(version_, reason_code, props);
        auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
        connected_ = false;
        close();
        // Wait for all internal coroutines (read_loop/keep_alive_loop/retry_loop) to exit
        while (active_loops_ > 0)
            co_await async_sleep(ctx_, std::chrono::milliseconds{10});
        if (!wr)
            co_return std::unexpected(wr.error().message());
        co_return std::expected<void, std::string>{};
    }

    // ----- Callbacks -----

    /// Register message arrival callback
    void on_message(message_callback cb) { msg_cb_ = std::move(cb); }

    /// Register disconnection callback
    void on_disconnect(disconnect_callback cb) { disconnect_cb_ = std::move(cb); }

    /// Register v5 AUTH callback (Enhanced Authentication)
    void on_auth(auth_callback cb) { auth_cb_ = std::move(cb); }

    /// Set auto-reconnect configuration
    void set_reconnect(reconnect_options opts) { reconnect_opts_ = std::move(opts); }

    /// Send AUTH packet (v5 only)
    auto send_auth(
        std::uint8_t reason_code = 0,
        const properties& props = {}
    ) -> task<std::expected<void, std::string>>
    {
        if (!connected_)
            co_return std::unexpected(std::string("not connected"));
        if (version_ != protocol_version::v5)
            co_return std::unexpected(std::string("AUTH requires MQTT v5"));
        auto pkt = encode_auth(reason_code, props);
        auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
        if (!wr)
            co_return std::unexpected(wr.error().message());
        co_return std::expected<void, std::string>{};
    }

    /// Get protocol version
    [[nodiscard]] auto version() const noexcept -> protocol_version { return version_; }

private:
    // ── Transport Layer ──

    auto do_write(const_buffer buf) -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) co_return co_await ssl_->async_write(buf);
#endif
        co_return co_await async_write(ctx_, sock_, buf);
    }

    auto do_read(mutable_buffer buf) -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) co_return co_await ssl_->async_read(buf);
#endif
        co_return co_await async_read(ctx_, sock_, buf);
    }

    // ── Frame Reading ──

    auto read_frame() -> task<std::expected<mqtt_frame, std::string>> {
        while (true) {
            auto frame = parser_.next();
            if (frame) co_return std::move(*frame);

            // Need more data
            std::array<std::byte, 8192> tmp{};
            auto r = co_await do_read(mutable_buffer{tmp.data(), tmp.size()});
            if (!r || *r == 0)
                co_return std::unexpected(std::string("connection closed"));
            parser_.feed(std::string_view(
                reinterpret_cast<const char*>(tmp.data()), *r));
        }
    }

    // ── Internal Read Loop ──

    auto read_loop() -> task<void> {
        ++active_loops_;
        while (connected_) {
            auto frame_r = co_await read_frame();
            if (!frame_r) {
                // If connected_ is already false, it's an intentional disconnect (disconnect() or close()),
                // no need to trigger auto-reconnect or call disconnect_cb_
                if (!connected_) break;
                connected_ = false;
                logger::warn("mqtt client read_loop error: {}", frame_r.error());
                if (disconnect_cb_) disconnect_cb_(frame_r.error());
                // Trigger auto-reconnect
                if (reconnect_opts_.enabled) {
                    reconnecting_ = true;
                    spawn(ctx_, auto_reconnect_loop());
                }
                break;
            }

            auto& frame = *frame_r;
            co_await dispatch_frame(frame);
        }
        --active_loops_;
    }

    // ── Frame Dispatch ──

    auto dispatch_frame(const mqtt_frame& frame) -> task<void> {
        switch (frame.type) {
        case control_packet_type::publish:
            co_await handle_publish(frame);
            break;

        case control_packet_type::puback:
        case control_packet_type::pubrec:
        case control_packet_type::pubcomp: {
            auto ack_r = decode_ack(frame.payload, version_);
            if (ack_r) {
                complete_pending(ack_r->packet_id, frame.type, frame.payload);
                // Remove acknowledged messages from inflight
                if (frame.type == control_packet_type::puback ||
                    frame.type == control_packet_type::pubcomp) {
                    std::erase_if(inflight_out_, [&](const inflight_message& im) {
                        return im.packet_id == ack_r->packet_id;
                    });
                }
            }
            break;
        }

        case control_packet_type::pubrel: {
            // QoS 2 inbound: Received PUBREL → Reply PUBCOMP, clear deduplication record
            auto ack_r = decode_ack(frame.payload, version_);
            if (ack_r) {
                qos2_received_.erase(ack_r->packet_id);
                auto pkt = encode_pubcomp(ack_r->packet_id, version_);
                (void)co_await do_write(const_buffer{pkt.data(), pkt.size()});
            }
            break;
        }

        case control_packet_type::suback: {
            auto sub_r = decode_suback(frame.payload, version_);
            if (sub_r) {
                complete_suback(sub_r->packet_id, std::move(*sub_r));
            }
            break;
        }

        case control_packet_type::unsuback: {
            auto unsub_r = decode_unsuback(frame.payload, version_);
            if (unsub_r) {
                complete_pending(unsub_r->packet_id, frame.type, frame.payload);
            }
            break;
        }

        case control_packet_type::pingresp:
            // PINGRESP — No special handling needed
            ping_outstanding_ = false;
            break;

        case control_packet_type::disconnect: {
            connected_ = false;
            if (version_ == protocol_version::v5) {
                auto dc = decode_disconnect(frame.payload, version_);
                if (disconnect_cb_)
                    disconnect_cb_(std::string("server disconnect: reason=") +
                                   std::to_string(dc.reason_code));
            } else {
                if (disconnect_cb_)
                    disconnect_cb_("server disconnect");
            }
            break;
        }

        case control_packet_type::auth: {
            // v5 AUTH — Enhanced Authentication
            if (version_ == protocol_version::v5) {
                auto auth_r = decode_auth(frame.payload);
                if (auth_cb_) {
                    auto resp = auth_cb_(auth_r.reason_code, auth_r.props);
                    if (resp) {
                        auto pkt = encode_auth(resp->first, resp->second);
                        (void)co_await do_write(const_buffer{pkt.data(), pkt.size()});
                    }
                }
            }
            break;
        }

        default:
            break;
        }
        co_return;
    }

    // ── PUBLISH Handling ──

    auto handle_publish(const mqtt_frame& frame) -> task<void> {
        auto msg_r = decode_publish(frame.payload, frame.flags, version_);
        if (!msg_r) co_return;

        auto& msg = *msg_r;

        // v5 Topic Alias resolution
        if (version_ == protocol_version::v5) {
            std::uint16_t alias = 0;
            for (auto& p : msg.props)
                if (p.id == property_id::topic_alias)
                    if (auto* v = std::get_if<std::uint16_t>(&p.value))
                        alias = *v;
            if (alias != 0) {
                auto resolved = alias_recv_.resolve(msg.topic, alias);
                if (resolved.empty()) {
                    logger::warn("mqtt client: invalid topic alias={}", alias);
                    co_return;
                }
                msg.topic = std::move(resolved);
            }
        }

        // QoS 1: Reply with PUBACK
        if (msg.qos_value == qos::at_least_once && msg.packet_id != 0) {
            auto pkt = encode_puback(msg.packet_id, version_);
            (void)co_await do_write(const_buffer{pkt.data(), pkt.size()});
        }

        // QoS 2: Reply with PUBREC (PUBREL handled in dispatch)
        if (msg.qos_value == qos::exactly_once && msg.packet_id != 0) {
            auto pkt = encode_pubrec(msg.packet_id, version_);
            (void)co_await do_write(const_buffer{pkt.data(), pkt.size()});
            // Deduplication: Only notify user on first receipt
            auto [_, inserted] = qos2_received_.insert(msg.packet_id);
            if (!inserted) co_return; // DUP — Already notified user
        }

        // Notify user
        if (msg_cb_) msg_cb_(msg);
        co_return;
    }

    // ── Packet ID Management ──

    auto alloc_packet_id() -> std::uint16_t {
        for (std::uint32_t attempt = 0; attempt < 65535; ++attempt) {
            auto id = next_packet_id_++;
            if (next_packet_id_ == 0) next_packet_id_ = 1;
            // Skip IDs still in inflight / pending
            bool in_use = false;
            for (auto& im : inflight_out_)
                if (im.packet_id == id) { in_use = true; break; }
            if (!in_use && pending_acks_.find(id) == pending_acks_.end()
                        && pending_subacks_.find(id) == pending_subacks_.end())
                return id;
        }
        return next_packet_id_++;
    }

    // ── ACK Waiting Mechanism ──

    struct pending_ack {
        control_packet_type expected_type;
        std::string         payload;
        bool                completed = false;
    };

    struct pending_suback_entry {
        suback_result result;
        bool          completed = false;
    };

    void complete_pending(std::uint16_t pid, control_packet_type type, std::string_view payload) {
        auto it = pending_acks_.find(pid);
        if (it != pending_acks_.end()) {
            it->second.expected_type = type;
            it->second.payload = std::string(payload);
            it->second.completed = true;
        }
    }

    void complete_suback(std::uint16_t pid, suback_result result) {
        auto it = pending_subacks_.find(pid);
        if (it != pending_subacks_.end()) {
            it->second.result = std::move(result);
            it->second.completed = true;
        }
    }

    /// Wait for ACK for specific packet_id
    auto wait_for_ack(std::uint16_t pid)
        -> task<std::expected<ack_result, std::string>>
    {
        pending_acks_[pid] = {};

        // Simple polling wait (yields in coroutine context)
        // Actually pushed to completion by read_loop
        for (int retry = 0; retry < 3000; ++retry) { // ~30 second timeout
            auto it = pending_acks_.find(pid);
            if (it != pending_acks_.end() && it->second.completed) {
                auto ack = decode_ack(it->second.payload, version_);
                pending_acks_.erase(it);
                if (!ack) co_return std::unexpected(ack.error());
                co_return *ack;
            }

            if (!connected_) {
                pending_acks_.erase(pid);
                co_return std::unexpected(std::string("disconnected while waiting for ACK"));
            }

            // yield to give read_loop a chance to run
            co_await async_sleep(ctx_, std::chrono::milliseconds(10));
        }

        pending_acks_.erase(pid);
        co_return std::unexpected(std::string("ACK timeout for packet_id=") +
                                  std::to_string(pid));
    }

    /// Wait for SUBACK
    auto wait_for_suback(std::uint16_t pid)
        -> task<std::expected<suback_result, std::string>>
    {
        pending_subacks_[pid] = {};

        for (int retry = 0; retry < 3000; ++retry) {
            auto it = pending_subacks_.find(pid);
            if (it != pending_subacks_.end() && it->second.completed) {
                auto result = std::move(it->second.result);
                pending_subacks_.erase(it);
                co_return result;
            }

            if (!connected_) {
                pending_subacks_.erase(pid);
                co_return std::unexpected(std::string("disconnected while waiting for SUBACK"));
            }

            co_await async_sleep(ctx_, std::chrono::milliseconds(10));
        }

        pending_subacks_.erase(pid);
        co_return std::unexpected(std::string("SUBACK timeout for packet_id=") +
                                  std::to_string(pid));
    }

    // ── Keep-alive ──

    auto send_ping() -> task<void> {
        if (!connected_ || keep_alive_sec_ == 0) co_return;
        auto pkt = encode_pingreq();
        (void)co_await do_write(const_buffer{pkt.data(), pkt.size()});
        ping_outstanding_ = true;
    }

    /// Auto keep-alive timer coroutine
    auto keep_alive_loop() -> task<void> {
        if (keep_alive_sec_ == 0) co_return;
        ++active_loops_;
        auto interval = std::chrono::milliseconds(
            static_cast<int>(keep_alive_sec_ * 750)); // 0.75 * keep_alive
        // PINGRESP wait timeout: half of keep_alive, ensures earlier detection than next PINGREQ
        auto ping_wait = std::chrono::milliseconds(
            static_cast<int>(keep_alive_sec_ * 500)); // 0.5 * keep_alive
        while (connected_) {
            co_await sleep_while_connected(interval);
            if (!connected_) break;

            if (ping_outstanding_) {
                // Previous PINGRESP not received → disconnect
                logger::warn("mqtt client keep-alive timeout");
                connected_ = false;
                if (disconnect_cb_) disconnect_cb_("keep-alive timeout");
                close();
                break;
            }

            co_await send_ping();

            // Wait shorter time after sending PINGREQ to detect PINGRESP timeout early
            co_await sleep_while_connected(ping_wait);
            if (!connected_) break;
            if (ping_outstanding_) {
                logger::warn("mqtt client PINGRESP timeout after {}ms", ping_wait.count());
                connected_ = false;
                if (disconnect_cb_) disconnect_cb_("PINGRESP timeout");
                close();
                break;
            }
        }
        --active_loops_;
    }

    // ── QoS Retransmission ──

    /// QoS message retransmission timer coroutine
    auto retry_loop() -> task<void> {
        ++active_loops_;
        while (connected_) {
            co_await sleep_while_connected(std::chrono::milliseconds(5000));
            if (!connected_) break;

            auto now = std::chrono::steady_clock::now();
            for (auto it = inflight_out_.begin(); it != inflight_out_.end(); ) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->send_time);
                if (elapsed < retry_interval_) { ++it; continue; }

                if (it->retry_count >= max_retries_) {
                    logger::warn("mqtt client retry exhausted pid={}", it->packet_id);
                    it = inflight_out_.erase(it);
                    continue;
                }

                // Resend (DUP=1)
                if (it->expected_ack == control_packet_type::puback ||
                    it->expected_ack == control_packet_type::pubrec) {
                    auto pkt = encode_publish(
                        it->msg.topic, it->msg.payload, it->msg.qos_value,
                        it->msg.retain, true, it->packet_id, version_, it->msg.props);
                    auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
                    if (!wr) { connected_ = false; break; }
                } else if (it->expected_ack == control_packet_type::pubcomp) {
                    auto pkt = encode_pubrel(it->packet_id, version_);
                    auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
                    if (!wr) { connected_ = false; break; }
                }

                it->send_time = now;
                ++it->retry_count;
                logger::debug("mqtt client retry pid={} attempt={}",
                    it->packet_id, it->retry_count);
                ++it;
            }
        }
        --active_loops_;
    }

    // ── Subscription Records (for reconnection restoration) ──

    void save_subscription(const subscribe_entry& entry) {
        for (auto& s : saved_subscriptions_)
            if (s.topic_filter == entry.topic_filter) {
                s = entry; // Update
                return;
            }
        saved_subscriptions_.push_back(entry);
    }

    void remove_saved_subscription(const std::string& filter) {
        std::erase_if(saved_subscriptions_, [&](const subscribe_entry& e) {
            return e.topic_filter == filter;
        });
    }

    // ── Auto-Reconnect ──

    auto auto_reconnect_loop() -> task<void> {
        auto delay = reconnect_opts_.initial_delay;
        std::uint32_t attempt = 0;

        while (reconnecting_) {
            if (reconnect_opts_.max_retries > 0 && attempt >= reconnect_opts_.max_retries) {
                logger::warn("mqtt client auto-reconnect exhausted after {} attempts", attempt);
                reconnecting_ = false;
                if (disconnect_cb_) disconnect_cb_("reconnect exhausted");
                co_return;
            }

            ++attempt;
            logger::info("mqtt client auto-reconnect attempt={} delay={}ms",
                attempt, delay.count());
            co_await async_sleep(ctx_, delay);

            if (!reconnecting_) co_return; // User intentionally disconnected or manually reconnected

            // Reset parser state
            parser_ = mqtt_parser{};
            ping_outstanding_ = false;

            auto r = co_await connect(last_connect_opts_);
            if (r) {
                logger::info("mqtt client auto-reconnect succeeded");
                reconnecting_ = false;
                // Restore subscriptions
                if (reconnect_opts_.restore_subscriptions && !saved_subscriptions_.empty()) {
                    auto sub_r = co_await subscribe(saved_subscriptions_);
                    if (sub_r)
                        logger::info("mqtt client restored {} subscriptions",
                            saved_subscriptions_.size());
                }
                co_return;
            }

            // Exponential backoff
            delay = std::chrono::milliseconds(static_cast<long long>(
                delay.count() * reconnect_opts_.backoff_multiplier));
            if (delay > reconnect_opts_.max_delay)
                delay = reconnect_opts_.max_delay;
        }
    }

    /// Session resumption: Resend inflight messages
    auto resend_inflight() -> task<void> {
        for (auto& im : inflight_out_) {
            if (im.expected_ack == control_packet_type::puback ||
                im.expected_ack == control_packet_type::pubrec) {
                auto pkt = encode_publish(
                    im.msg.topic, im.msg.payload, im.msg.qos_value,
                    im.msg.retain, true, im.packet_id, version_, im.msg.props);
                auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
                if (!wr) break;
                im.send_time = std::chrono::steady_clock::now();
            } else if (im.expected_ack == control_packet_type::pubcomp) {
                auto pkt = encode_pubrel(im.packet_id, version_);
                auto wr = co_await do_write(const_buffer{pkt.data(), pkt.size()});
                if (!wr) break;
            }
        }
    }

    // ── Internal Utilities ──

    /// Interruptible sleep: Sleep in chunks, exit quickly after disconnect
    auto sleep_while_connected(std::chrono::milliseconds duration) -> task<void> {
        constexpr auto chunk = std::chrono::milliseconds(200);
        while (duration.count() > 0 && connected_) {
            auto d = std::min(duration, chunk);
            co_await async_sleep(ctx_, d);
            duration -= d;
        }
    }

    // ── Members ──

    io_context&      ctx_;
    socket           sock_;
    mqtt_parser      parser_;
    protocol_version version_       = protocol_version::v3_1_1;
    bool             connected_     = false;
    bool             session_present_ = false;
    std::uint16_t    keep_alive_sec_ = 60;
    std::uint16_t    next_packet_id_ = 1;
    bool             ping_outstanding_ = false;

    // CONNACK v5 properties
    properties       connack_props_;

    // v5 Topic Alias
    topic_alias_send alias_send_{0};
    topic_alias_recv alias_recv_{0};

    // v5 Receive Maximum / Maximum Packet Size
    std::uint16_t    receive_maximum_  = 65535;
    std::size_t      max_packet_size_  = 0;     // 0=unlimited

    // QoS inflight retransmission
    std::vector<inflight_message> inflight_out_;
    std::set<std::uint16_t>       qos2_received_;  // QoS 2 inbound deduplication
    std::chrono::seconds retry_interval_{20};
    std::uint8_t         max_retries_ = 5;

    // Callbacks
    message_callback    msg_cb_;
    disconnect_callback disconnect_cb_;
    auth_callback       auth_cb_;

    // Auto-Reconnect
    reconnect_options   reconnect_opts_;
    connect_options     last_connect_opts_;
    std::vector<subscribe_entry> saved_subscriptions_;
    bool                reconnecting_ = false;

    // Internal coroutine lifecycle tracking
    int              active_loops_  = 0;

    // ACK waiting maps
    std::map<std::uint16_t, pending_ack>          pending_acks_;
    std::map<std::uint16_t, pending_suback_entry> pending_subacks_;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream>  ssl_;
#endif
};

} // namespace cnetmod::mqtt
