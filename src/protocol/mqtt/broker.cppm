/// cnetmod.protocol.mqtt:broker — MQTT Broker 核心
/// TCP MQTT Broker，支持 v3.1.1 和 v5.0
/// channel-based 双协程模型实时投递，trie 订阅匹配，security/ACL，TLS

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
// Broker 配置
// =============================================================================

export struct broker_options {
    std::uint16_t port                   = 1883;
    std::string   host                   = "127.0.0.1";
    std::uint16_t max_connections        = 10000;
    std::uint32_t default_session_expiry = 0;     // 秒, 0=clean session 立即过期
    std::uint16_t max_keep_alive         = 600;   // 最大 keep-alive 秒数
    std::size_t   max_inflight_messages  = 100;   // 每连接最大 inflight 消息数
    std::size_t   delivery_channel_size  = 1000;  // 投递 channel 容量
    std::uint16_t topic_alias_maximum    = 0;     // 服务端支持的 topic alias 最大数

    // v5 能力通告
    std::uint16_t receive_maximum        = 65535; // 服务端允许的最大 inflight 数
    std::uint32_t maximum_packet_size    = 0;     // 服务端最大包大小, 0=无限制
    qos           maximum_qos            = qos::exactly_once; // 服务端支持的最大 QoS
    bool          retain_available       = true;
    bool          wildcard_sub_available = true;
    bool          sub_id_available       = true;
    bool          shared_sub_available   = true;

    // TLS 配置
    std::uint16_t tls_port               = 0;     // 0=不启用 TLS 监听
    std::string   tls_cert_file;
    std::string   tls_key_file;
    std::string   tls_ca_file;
};

// =============================================================================
// 连接状态 (每连接，shared_ptr 共享于 read_loop 和 delivery_loop)
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

    // 投递 channel: route_publish → delivery_loop
    channel<publish_message> delivery_ch;

    // v5 Topic Alias (接收端)
    topic_alias_recv alias_recv{0};

    // v5 Maximum Packet Size (客户端告知的最大包大小)
    std::size_t max_packet_size = 0; // 0=无限制

    // Keep-alive 超时跟踪
    std::chrono::steady_clock::time_point last_packet_time
        = std::chrono::steady_clock::now();

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_stream> ssl;
#endif

    conn_state(socket s, io_context& ctx, std::size_t ch_cap)
        : sock(std::move(s)), io(ctx), delivery_ch(ch_cap) {}

    auto do_write(const std::string& data)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl) co_return co_await ssl->async_write(
            const_buffer{data.data(), data.size()});
#endif
        co_return co_await async_write(io, sock,
            const_buffer{data.data(), data.size()});
    }

    auto do_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl) co_return co_await ssl->async_read(buf);
#endif
        co_return co_await async_read(io, sock, buf);
    }
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
        -> std::expected<void, std::error_code>
    {
        auto addr_r = ip_address::from_string(host);
        if (!addr_r) return std::unexpected(addr_r.error());
        acc_ = std::make_unique<tcp::acceptor>(ctx_);
        auto r = acc_->open(endpoint{*addr_r, port}, {.reuse_address = true, .non_blocking = true});
        if (!r) return std::unexpected(r.error());
        logger::info("mqtt broker listening on {}:{}", host, port);
        return {};
    }

#ifdef CNETMOD_HAS_SSL
    auto listen_tls(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>
    {
        auto addr_r = ip_address::from_string(host);
        if (!addr_r) return std::unexpected(addr_r.error());

        auto ssl_ctx_r = ssl_context::server();
        if (!ssl_ctx_r) return std::unexpected(ssl_ctx_r.error());
        ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));

        if (!opts_.tls_cert_file.empty()) {
            auto r2 = ssl_ctx_->load_cert_file(opts_.tls_cert_file);
            if (!r2) return std::unexpected(r2.error());
        }
        if (!opts_.tls_key_file.empty()) {
            auto r2 = ssl_ctx_->load_key_file(opts_.tls_key_file);
            if (!r2) return std::unexpected(r2.error());
        }
        if (!opts_.tls_ca_file.empty()) {
            auto r2 = ssl_ctx_->load_ca_file(opts_.tls_ca_file);
            if (!r2) return std::unexpected(r2.error());
        }

        tls_acc_ = std::make_unique<tcp::acceptor>(ctx_);
        auto r = tls_acc_->open(endpoint{*addr_r, port}, {.reuse_address = true, .non_blocking = true});
        if (!r) return std::unexpected(r.error());
        logger::info("mqtt broker TLS listening on {}:{}", host, port);
        return {};
    }

    auto listen_tls() -> std::expected<void, std::error_code> {
        if (opts_.tls_port == 0) return {};
        return listen_tls(opts_.host, opts_.tls_port);
    }
#endif

    auto run() -> task<void> {
        running_ = true;
#ifdef CNETMOD_HAS_SSL
        if (tls_acc_) spawn(ctx_, run_tls_accept());
#endif
        // 启动会话过期清理定时器
        spawn(ctx_, session_expiry_loop());
        while (running_) {
            auto r = co_await async_accept(ctx_, acc_->native_socket());
            if (!r) { if (!running_) break; continue; }

            if (sctx_) {
                auto& worker = sctx_->next_worker_io();
                spawn_on(worker, handle_connection(std::move(*r), worker, false));
            } else {
                spawn(ctx_, handle_connection(std::move(*r), ctx_, false));
            }
        }
    }

    void stop() {
        running_ = false;
        if (acc_) acc_->close();
#ifdef CNETMOD_HAS_SSL
        if (tls_acc_) tls_acc_->close();
#endif
        while (!channels_rw_.try_lock())
            std::this_thread::yield();
        for (auto& [cid, ch] : online_channels_) ch->close();
        online_channels_.clear();
        channels_rw_.unlock();
        logger::info("mqtt broker stopped");
    }

private:

#ifdef CNETMOD_HAS_SSL
    auto run_tls_accept() -> task<void> {
        while (running_) {
            auto r = co_await async_accept(ctx_, tls_acc_->native_socket());
            if (!r) { if (!running_) break; continue; }
            if (sctx_) {
                auto& worker = sctx_->next_worker_io();
                spawn_on(worker, handle_connection(std::move(*r), worker, true));
            } else {
                spawn(ctx_, handle_connection(std::move(*r), ctx_, true));
            }
        }
    }
#endif

    // =========================================================================
    // Session 过期清理
    // =========================================================================

    auto session_expiry_loop() -> task<void> {
        while (running_) {
            co_await async_sleep(ctx_, std::chrono::seconds(60));
            if (!running_) break;

            // 收集待清理的过期会话 client_id
            std::vector<std::string> expired;
            sessions_.for_each([&](const session_state& ss) {
                if (ss.is_expired())
                    expired.push_back(ss.client_id);
            });

            for (auto& cid : expired) {
                sub_map_.erase_client(cid);
                shared_store_.remove_client(cid);
                sessions_.remove(cid);
                logger::debug("mqtt session expired and removed: {}", cid);
            }
        }
    }

    // =========================================================================
    // Keep-alive watchdog
    // =========================================================================

    auto keep_alive_watchdog(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        // MQTT 规范: 服务端在 1.5 × keep_alive 秒内未收到任何报文时应断开
        auto timeout = std::chrono::milliseconds(
            static_cast<int>(conn->keep_alive * 1500));
        while (conn->connected) {
            co_await async_sleep(conn->io, std::chrono::seconds(conn->keep_alive));
            if (!conn->connected) break;

            auto elapsed = std::chrono::steady_clock::now() - conn->last_packet_time;
            if (elapsed >= timeout) {
                logger::warn("mqtt keep-alive timeout client={}",
                    conn->session ? conn->session->client_id : "?");
                conn->connected = false;
                conn->delivery_ch.close();
                conn->sock.close();
                co_return;
            }
        }
    }

    // =========================================================================
    // 帧读取
    // =========================================================================

    static auto read_frame(detail::conn_state& conn)
        -> task<std::expected<mqtt_frame, std::string>>
    {
        while (true) {
            auto frame = conn.parser.next();
            if (frame) co_return std::move(*frame);

            std::array<std::byte, 8192> tmp{};
            auto r = co_await conn.do_read(mutable_buffer{tmp.data(), tmp.size()});
            if (!r || *r == 0)
                co_return std::unexpected(std::string("connection closed"));
            conn.parser.feed(std::string_view(
                reinterpret_cast<const char*>(tmp.data()), *r));
        }
    }

    // =========================================================================
    // 连接处理 — 双协程模型
    // =========================================================================

    auto handle_connection(socket client, io_context& io, bool use_tls) -> task<void> {
        auto conn = std::make_shared<detail::conn_state>(
            std::move(client), io, opts_.delivery_channel_size);

#ifdef CNETMOD_HAS_SSL
        if (use_tls && ssl_ctx_) {
            conn->ssl = std::make_unique<ssl_stream>(*ssl_ctx_, io, conn->sock);
            conn->ssl->set_accept_state();
            auto hs = co_await conn->ssl->async_handshake();
            if (!hs) {
                logger::debug("mqtt tls handshake failed: {}", hs.error().message());
                conn->sock.close();
                co_return;
            }
        }
#else
        (void)use_tls;
#endif

        // 等待 CONNECT 报文
        auto frame_r = co_await read_frame(*conn);
        if (!frame_r) { conn->sock.close(); co_return; }

        if (frame_r->type != control_packet_type::connect) {
            conn->sock.close();
            co_return;
        }

        auto ok = co_await handle_connect(*frame_r, conn);
        if (!ok) { conn->sock.close(); co_return; }

        // 启动 delivery_loop + retry_loop 协程
        spawn(io, delivery_loop(conn));
        spawn(io, retry_loop(conn));

        // 启动 keep-alive watchdog
        if (conn->keep_alive > 0)
            spawn(io, keep_alive_watchdog(conn));

        // read_loop
        while (conn->connected) {
            auto fr = co_await read_frame(*conn);
            if (!fr) {
                co_await handle_unexpected_disconnect(conn);
                co_return;
            }

            // 更新最后收包时间
            conn->last_packet_time = std::chrono::steady_clock::now();

            switch (fr->type) {
            case control_packet_type::connect:
                // MQTT 规范: 同一连接上收到第二个 CONNECT 报文是协议错误
                logger::warn("mqtt duplicate CONNECT from client={}",
                    conn->session ? conn->session->client_id : "?");
                co_await send_disconnect_and_close(conn,
                    static_cast<std::uint8_t>(v5::disconnect_reason_code::protocol_error));
                co_return;
            case control_packet_type::publish:
                co_await handle_publish(*fr, conn);         break;
            case control_packet_type::puback:
                co_await handle_puback(*fr, conn);          break;
            case control_packet_type::pubrec:
                co_await handle_pubrec(*fr, conn);          break;
            case control_packet_type::pubrel:
                co_await handle_pubrel(*fr, conn);          break;
            case control_packet_type::pubcomp:
                co_await handle_pubcomp(*fr, conn);         break;
            case control_packet_type::subscribe:
                co_await handle_subscribe(*fr, conn);       break;
            case control_packet_type::unsubscribe:
                co_await handle_unsubscribe(*fr, conn);     break;
            case control_packet_type::pingreq:
                co_await handle_pingreq(conn);              break;
            case control_packet_type::disconnect:
                co_await handle_disconnect(*fr, conn);
                co_return;
            case control_packet_type::auth:
                co_await handle_auth(*fr, conn);
                break;
            default: break;
            }
        }
        conn->sock.close();
    }

    // =========================================================================
    // delivery_loop — 从 channel 读取并写入 socket
    // =========================================================================

    auto delivery_loop(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        while (conn->connected) {
            auto msg_opt = co_await conn->delivery_ch.receive();
            if (!msg_opt) break;

            if (!conn->connected) break;

            auto& msg = *msg_opt;

            // Receive Maximum 流控
            if (msg.qos_value != qos::at_most_once && conn->session) {
                if (conn->session->inflight_out.size() >=
                    static_cast<std::size_t>(conn->session->receive_maximum)) {
                    conn->session->enqueue_offline(std::move(msg));
                    continue;
                }
            }

            std::uint16_t pid = 0;
            if (msg.qos_value != qos::at_most_once && conn->session)
                pid = conn->session->alloc_packet_id();

            auto pkt = encode_publish(
                msg.topic, msg.payload, msg.qos_value,
                false, false, pid, conn->version, msg.props);

            // Maximum Packet Size 检查
            if (conn->max_packet_size > 0 && pkt.size() > conn->max_packet_size) {
                logger::debug("mqtt delivery: packet too large for client={}",
                    conn->session ? conn->session->client_id : "?");
                continue; // 丢弃超限消息
            }

            auto wr = co_await conn->do_write(pkt);
            if (!wr) { conn->connected = false; break; }

            if (msg.qos_value != qos::at_most_once && conn->session) {
                inflight_message im;
                im.packet_id    = pid;
                im.msg          = msg;
                im.expected_ack = (msg.qos_value == qos::at_least_once)
                    ? control_packet_type::puback : control_packet_type::pubrec;
                im.send_time    = std::chrono::steady_clock::now();
                conn->session->inflight_out.push_back(std::move(im));
            }
        }
    }

    // =========================================================================
    // retry_loop — QoS 消息重传 (Phase 2.1)
    // =========================================================================

    auto retry_loop(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        while (conn->connected) {
            co_await async_sleep(conn->io, std::chrono::seconds(5));
            if (!conn->connected || !conn->session) break;

            auto now = std::chrono::steady_clock::now();
            auto& inflight = conn->session->inflight_out;

            for (auto it = inflight.begin(); it != inflight.end(); ) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->send_time);
                if (elapsed < conn->session->retry_interval) { ++it; continue; }

                if (it->retry_count >= conn->session->max_retries) {
                    logger::warn("mqtt retry exhausted client={} pid={}",
                        conn->session->client_id, it->packet_id);
                    conn->session->release_packet_id(it->packet_id);
                    it = inflight.erase(it);
                    continue;
                }

                // 重发 (DUP=1)
                auto pkt = encode_publish(
                    it->msg.topic, it->msg.payload, it->msg.qos_value,
                    false, true, it->packet_id, conn->version, it->msg.props);

                // Maximum Packet Size 检查
                if (conn->max_packet_size > 0 && pkt.size() > conn->max_packet_size) {
                    conn->session->release_packet_id(it->packet_id);
                    it = inflight.erase(it);
                    continue;
                }

                auto wr = co_await conn->do_write(pkt);
                if (!wr) { conn->connected = false; break; }

                it->send_time = now;
                ++it->retry_count;
                logger::debug("mqtt retry client={} pid={} attempt={}",
                    conn->session->client_id, it->packet_id, it->retry_count);
                ++it;
            }
        }
    }

    // =========================================================================
    // CONNECT 处理
    // =========================================================================

    auto handle_connect(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<bool>
    {
        auto cd_r = decode_connect(frame.payload);
        if (!cd_r) {
            auto pkt = encode_connack(false,
                static_cast<std::uint8_t>(connect_return_code::unacceptable_protocol_version),
                protocol_version::v3_1_1);
            (void)co_await conn->do_write(pkt);
            co_return false;
        }

        auto& cd = *cd_r;
        conn->version   = cd.version;
        conn->keep_alive = cd.keep_alive;

        // 安全认证
        if (security_.enabled()) {
            auto auth_user = security_.authenticate(cd.username, cd.password);
            if (!auth_user) {
                logger::warn("mqtt auth failed user={}", cd.username);
                auto rc = (cd.version == protocol_version::v5)
                    ? static_cast<std::uint8_t>(v5::connect_reason_code::bad_user_name_or_password)
                    : static_cast<std::uint8_t>(connect_return_code::bad_user_name_or_password);
                (void)co_await conn->do_write(encode_connack(false, rc, cd.version));
                co_return false;
            }
            cd.username = *auth_user;
        }

        // client_id 处理
        bool cd_client_id_was_empty = cd.client_id.empty();
        if (cd.client_id.empty()) {
            if (cd.clean_session) {
                cd.client_id = generate_client_id();
            } else {
                auto rc = (cd.version == protocol_version::v5)
                    ? static_cast<std::uint8_t>(v5::connect_reason_code::client_identifier_not_valid)
                    : static_cast<std::uint8_t>(connect_return_code::identifier_rejected);
                (void)co_await conn->do_write(encode_connack(false, rc, cd.version));
                co_return false;
            }
        }

        // Session takeover
        auto* existing = sessions_.find(cd.client_id);
        if (existing && existing->online) {
            logger::info("mqtt session takeover client={}", cd.client_id);
            existing->go_offline();
            {
                co_await channels_rw_.lock();
                async_unique_lock_guard wg(channels_rw_, std::adopt_lock);
                auto it = online_channels_.find(cd.client_id);
                if (it != online_channels_.end()) {
                    it->second->close();
                    online_channels_.erase(it);
                }
            }
        }

        // 创建或恢复会话
        auto [ss, session_present] = sessions_.create_or_resume(
            cd.client_id, cd.clean_session, cd.version);

        ss.keep_alive = conn->keep_alive;
        ss.will_msg   = cd.will_msg;
        ss.username   = cd.username;

        // v5 属性
        if (cd.version == protocol_version::v5) {
            for (auto& p : cd.props) {
                if (p.id == property_id::session_expiry_interval)
                    if (auto* v = std::get_if<std::uint32_t>(&p.value))
                        ss.session_expiry_interval = *v;
                if (p.id == property_id::receive_maximum)
                    if (auto* v = std::get_if<std::uint16_t>(&p.value)) {
                        ss.receive_maximum = *v;
                        ss.inflight_quota  = *v;
                    }
                // 客户端 topic_alias_maximum → 服务端可向该客户端发送的别名上限 (暂不使用)
                if (p.id == property_id::maximum_packet_size)
                    if (auto* v = std::get_if<std::uint32_t>(&p.value))
                        conn->max_packet_size = static_cast<std::size_t>(*v);
            }
            // 服务端 opts_.topic_alias_maximum → 服务端可接收的别名上限 (已在 CONNACK 通告)
            conn->alias_recv.set_max(opts_.topic_alias_maximum);
        }

        conn->session  = &ss;
        conn->connected = true;

        // 注册在线 channel
        {
            co_await channels_rw_.lock();
            async_unique_lock_guard wg(channels_rw_, std::adopt_lock);
            online_channels_[cd.client_id] = &conn->delivery_ch;
        }

        // 订阅管理
        if (cd.clean_session) {
            sub_map_.erase_client(cd.client_id);
            shared_store_.remove_client(cd.client_id);
        } else if (session_present) {
            for (auto& [filter, entry] : ss.subscriptions) {
                auto shared = parse_shared_subscription(filter);
                if (shared && !shared->share_name.empty()) {
                    sub_map_.insert(shared->topic_filter, cd.client_id, entry);
                    shared_store_.add_member(shared->share_name,
                        shared->topic_filter, cd.client_id);
                } else {
                    sub_map_.insert(filter, cd.client_id, entry);
                }
            }
        }

        // 服务端可覆盖 keep-alive
        if (cd.version == protocol_version::v5 && opts_.max_keep_alive > 0
            && conn->keep_alive > opts_.max_keep_alive) {
            conn->keep_alive = opts_.max_keep_alive;
            ss.keep_alive = conn->keep_alive;
        }

        // CONNACK
        properties connack_props;
        if (cd.version == protocol_version::v5) {
            // Assigned Client Identifier
            if (cd_client_id_was_empty)
                connack_props.push_back(
                    mqtt_property::string_prop(property_id::assigned_client_identifier, cd.client_id));
            // Topic Alias Maximum
            if (opts_.topic_alias_maximum > 0)
                connack_props.push_back({property_id::topic_alias_maximum,
                    opts_.topic_alias_maximum});
            // Server Keep Alive (如果被覆盖)
            if (opts_.max_keep_alive > 0 && conn->keep_alive != cd.keep_alive)
                connack_props.push_back({property_id::server_keep_alive, conn->keep_alive});
            // Receive Maximum
            if (opts_.receive_maximum < 65535)
                connack_props.push_back({property_id::receive_maximum, opts_.receive_maximum});
            // Maximum Packet Size
            if (opts_.maximum_packet_size > 0)
                connack_props.push_back({property_id::maximum_packet_size, opts_.maximum_packet_size});
            // Maximum QoS
            if (opts_.maximum_qos != qos::exactly_once)
                connack_props.push_back(
                    mqtt_property::byte_prop(property_id::maximum_qos,
                        static_cast<std::uint8_t>(opts_.maximum_qos)));
            // Retain Available
            if (!opts_.retain_available)
                connack_props.push_back(
                    mqtt_property::byte_prop(property_id::retain_available, 0));
            // Wildcard Subscription Available
            if (!opts_.wildcard_sub_available)
                connack_props.push_back(
                    mqtt_property::byte_prop(property_id::wildcard_subscription_available, 0));
            // Subscription Identifier Available
            if (!opts_.sub_id_available)
                connack_props.push_back(
                    mqtt_property::byte_prop(property_id::subscription_identifier_available, 0));
            // Shared Subscription Available
            if (!opts_.shared_sub_available)
                connack_props.push_back(
                    mqtt_property::byte_prop(property_id::shared_subscription_available, 0));
        }

        auto rc = (cd.version == protocol_version::v5)
            ? static_cast<std::uint8_t>(v5::connect_reason_code::success)
            : static_cast<std::uint8_t>(connect_return_code::accepted);

        auto wr = co_await conn->do_write(
            encode_connack(session_present, rc, cd.version, connack_props));
        if (!wr) co_return false;

        logger::info("mqtt connected client={} version={} session_present={}",
            cd.client_id, to_string(cd.version), session_present);

        // Phase 2.2: inflight 重发
        if (session_present)
            co_await deliver_inflight_resend(conn);

        co_await deliver_offline_queue(conn);
        co_return true;
    }

    // =========================================================================
    // PUBLISH 处理
    // =========================================================================

    auto handle_publish(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        auto msg_r = decode_publish(frame.payload, frame.flags, conn->version);
        if (!msg_r) {
            logger::warn("mqtt decode PUBLISH failed: {}", msg_r.error());
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
            co_return;
        }
        auto& msg = *msg_r;

        // Maximum QoS 强制检查
        if (static_cast<std::uint8_t>(msg.qos_value) >
            static_cast<std::uint8_t>(opts_.maximum_qos)) {
            logger::warn("mqtt PUBLISH qos={} exceeds server maximum_qos={}",
                to_string(msg.qos_value), to_string(opts_.maximum_qos));
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::qos_not_supported));
            co_return;
        }

        // v5 Topic Alias
        if (conn->version == protocol_version::v5) {
            std::uint16_t alias = 0;
            for (auto& p : msg.props)
                if (p.id == property_id::topic_alias)
                    if (auto* v = std::get_if<std::uint16_t>(&p.value))
                        alias = *v;
            if (alias != 0) {
                auto resolved = conn->alias_recv.resolve(msg.topic, alias);
                if (resolved.empty()) {
                    logger::warn("mqtt invalid topic alias={}", alias);
                    co_return;
                }
                msg.topic = std::move(resolved);
            }
        }

        if (!validate_topic_name(msg.topic)) co_return;

        // ACL 检查
        if (security_.enabled() && conn->session)
            if (!security_.authorize_publish(conn->session->username, msg.topic)) {
                logger::warn("mqtt publish denied user={} topic={}",
                    conn->session->username, msg.topic);
                co_return;
            }

        // QoS 1 → PUBACK
        if (msg.qos_value == qos::at_least_once && msg.packet_id != 0) {
            (void)co_await conn->do_write(encode_puback(msg.packet_id, conn->version));
        }

        // QoS 2 → PUBREC, 暂存消息等 PUBREL 后转发
        if (msg.qos_value == qos::exactly_once && msg.packet_id != 0) {
            if (conn->session) {
                conn->session->qos2_received.insert(msg.packet_id);
                conn->session->qos2_pending_publish[msg.packet_id] = msg;
            }
            (void)co_await conn->do_write(encode_pubrec(msg.packet_id, conn->version));
            co_return;
        }

        if (msg.retain)
            retained_.store(msg.topic, retained_message{
                msg.topic, msg.payload, msg.qos_value, msg.props});

        co_await route_publish(msg, conn->session ? conn->session->client_id : "");
    }

    // =========================================================================
    // ACK 处理
    // =========================================================================

    auto handle_puback(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        auto ack_r = decode_ack(frame.payload, conn->version);
        if (!ack_r) {
            logger::warn("mqtt decode PUBACK failed: {}", ack_r.error());
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
            co_return;
        }
        if (!conn->session) co_return;
        conn->session->release_packet_id(ack_r->packet_id);
        std::erase_if(conn->session->inflight_out, [&](const inflight_message& im) {
            return im.packet_id == ack_r->packet_id;
        });
    }

    auto handle_pubrec(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        auto ack_r = decode_ack(frame.payload, conn->version);
        if (!ack_r) {
            logger::warn("mqtt decode PUBREC failed: {}", ack_r.error());
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
            co_return;
        }
        (void)co_await conn->do_write(encode_pubrel(ack_r->packet_id, conn->version));
        if (conn->session)
            for (auto& im : conn->session->inflight_out)
                if (im.packet_id == ack_r->packet_id) {
                    im.expected_ack = control_packet_type::pubcomp;
                    break;
                }
    }

    auto handle_pubrel(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        auto ack_r = decode_ack(frame.payload, conn->version);
        if (!ack_r) {
            logger::warn("mqtt decode PUBREL failed: {}", ack_r.error());
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
            co_return;
        }
        (void)co_await conn->do_write(encode_pubcomp(ack_r->packet_id, conn->version));
        if (conn->session) {
            conn->session->qos2_received.erase(ack_r->packet_id);
            // 取出暂存的 QoS 2 消息并转发
            auto it = conn->session->qos2_pending_publish.find(ack_r->packet_id);
            if (it != conn->session->qos2_pending_publish.end()) {
                auto msg = std::move(it->second);
                conn->session->qos2_pending_publish.erase(it);
                if (msg.retain)
                    retained_.store(msg.topic, retained_message{
                        msg.topic, msg.payload, msg.qos_value, msg.props});
                co_await route_publish(msg, conn->session->client_id);
            }
        }
    }

    auto handle_pubcomp(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        auto ack_r = decode_ack(frame.payload, conn->version);
        if (!ack_r) {
            logger::warn("mqtt decode PUBCOMP failed: {}", ack_r.error());
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
            co_return;
        }
        if (!conn->session) co_return;
        conn->session->release_packet_id(ack_r->packet_id);
        std::erase_if(conn->session->inflight_out, [&](const inflight_message& im) {
            return im.packet_id == ack_r->packet_id;
        });
    }

    // =========================================================================
    // SUBSCRIBE 处理
    // =========================================================================

    auto handle_subscribe(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        auto sub_r = decode_subscribe(frame.payload, conn->version);
        if (!sub_r) {
            logger::warn("mqtt decode SUBSCRIBE failed: {}", sub_r.error());
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
            co_return;
        }
        if (!conn->session) co_return;

        auto& sub = *sub_r;
        std::vector<std::uint8_t> return_codes;

        for (auto& entry : sub.entries) {
            if (!validate_topic_filter(entry.topic_filter)) {
                return_codes.push_back(conn->version == protocol_version::v5
                    ? static_cast<std::uint8_t>(v5::suback_reason_code::topic_filter_invalid)
                    : static_cast<std::uint8_t>(suback_return_code::failure));
                continue;
            }

            // ACL 检查
            if (security_.enabled()) {
                auto actual_filter = extract_topic_filter(entry.topic_filter);
                if (!security_.authorize_subscribe(conn->session->username, actual_filter)) {
                    logger::warn("mqtt subscribe denied user={} filter={}",
                        conn->session->username, entry.topic_filter);
                    return_codes.push_back(conn->version == protocol_version::v5
                        ? static_cast<std::uint8_t>(v5::suback_reason_code::not_authorized)
                        : static_cast<std::uint8_t>(suback_return_code::failure));
                    continue;
                }
            }

            bool is_new = conn->session->add_subscription(entry);

            // 注册到 trie + shared store
            auto shared = parse_shared_subscription(entry.topic_filter);
            if (shared && !shared->share_name.empty()) {
                sub_map_.insert(shared->topic_filter, conn->session->client_id, entry);
                shared_store_.add_member(shared->share_name, shared->topic_filter,
                    conn->session->client_id);
            } else {
                sub_map_.insert(entry.topic_filter, conn->session->client_id, entry);
            }

            return_codes.push_back(static_cast<std::uint8_t>(entry.max_qos));

            if (is_new || entry.rh == retain_handling::send) {
                auto actual = extract_topic_filter(entry.topic_filter);
                co_await deliver_retained(conn, entry, actual);
            }
        }

        (void)co_await conn->do_write(
            encode_suback(sub.packet_id, return_codes, conn->version, sub.props));
    }

    // =========================================================================
    // UNSUBSCRIBE 处理
    // =========================================================================

    auto handle_unsubscribe(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        auto unsub_r = decode_unsubscribe(frame.payload, conn->version);
        if (!unsub_r) {
            logger::warn("mqtt decode UNSUBSCRIBE failed: {}", unsub_r.error());
            co_await send_disconnect_and_close(conn,
                static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
            co_return;
        }
        if (!conn->session) co_return;

        auto& unsub = *unsub_r;
        std::vector<std::uint8_t> reason_codes;

        for (auto& tf : unsub.topic_filters) {
            bool existed = conn->session->remove_subscription(tf);
            auto shared = parse_shared_subscription(tf);
            if (shared && !shared->share_name.empty()) {
                sub_map_.erase(shared->topic_filter, conn->session->client_id);
                shared_store_.remove_member(shared->share_name,
                    shared->topic_filter, conn->session->client_id);
            } else {
                sub_map_.erase(tf, conn->session->client_id);
            }
            if (conn->version == protocol_version::v5)
                reason_codes.push_back(existed
                    ? static_cast<std::uint8_t>(v5::unsuback_reason_code::success)
                    : static_cast<std::uint8_t>(v5::unsuback_reason_code::no_subscription_existed));
        }

        (void)co_await conn->do_write(
            encode_unsuback(unsub.packet_id, conn->version, reason_codes));
    }

    // =========================================================================
    // PINGREQ / DISCONNECT
    // =========================================================================

    auto handle_pingreq(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        (void)co_await conn->do_write(encode_pingresp());
    }

    auto handle_disconnect(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        conn->connected = false;
        if (conn->session) {
            conn->session->will_msg.reset();
            if (conn->version == protocol_version::v5 && !frame.payload.empty()) {
                auto dc = decode_disconnect(frame.payload, conn->version);
                for (auto& p : dc.props)
                    if (p.id == property_id::session_expiry_interval)
                        if (auto* v = std::get_if<std::uint32_t>(&p.value))
                            conn->session->session_expiry_interval = *v;
            }
            co_await cleanup_connection(conn);
        }
        conn->delivery_ch.close();
        conn->sock.close();
        co_return;
    }

    // =========================================================================
    // AUTH 处理 (v5 Enhanced Authentication)
    // =========================================================================

    auto handle_auth(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        if (conn->version != protocol_version::v5) co_return;
        auto auth_r = decode_auth(frame.payload);
        if (auth_handler_ && conn->session) {
            auto resp = auth_handler_(
                conn->session->client_id, auth_r.reason_code, auth_r.props);
            if (resp) {
                (void)co_await conn->do_write(
                    encode_auth(resp->first, resp->second));
            }
        }
    }

    // =========================================================================
    // 非预期断连
    // =========================================================================

    auto handle_unexpected_disconnect(std::shared_ptr<detail::conn_state> conn)
        -> task<void>
    {
        conn->connected = false;
        if (conn->session) {
            logger::info("mqtt unexpected disconnect client={}", conn->session->client_id);

            if (conn->session->will_msg) {
                // Phase 3.5: Will Delay Interval
                std::uint32_t will_delay = 0;
                for (auto& p : conn->session->will_msg->props)
                    if (p.id == property_id::will_delay_interval)
                        if (auto* v = std::get_if<std::uint32_t>(&p.value))
                            will_delay = *v;

                if (will_delay > 0) {
                    auto cid = conn->session->client_id;
                    auto will_copy = *conn->session->will_msg;
                    conn->session->will_msg.reset();
                    spawn(conn->io, will_delay_task(conn->io, cid, std::move(will_copy), will_delay));
                } else {
                    co_await publish_will(conn);
                }
            }
            co_await cleanup_connection(conn);
        }
        conn->delivery_ch.close();
        conn->sock.close();
    }

    // =========================================================================
    // Will Delay (Phase 3.5)
    // =========================================================================

    auto will_delay_task(io_context& io, std::string client_id,
                         will will_msg, std::uint32_t delay_sec) -> task<void>
    {
        co_await async_sleep(io, std::chrono::seconds(delay_sec));
        auto* ss = sessions_.find(client_id);
        if (ss && ss->online) co_return; // 已重连

        publish_message wp;
        wp.topic = will_msg.topic;   wp.payload = will_msg.message;
        wp.qos_value = will_msg.qos_value; wp.retain = will_msg.retain;
        wp.props = will_msg.props;
        if (wp.retain) retained_.store(wp.topic, retained_message{
            wp.topic, wp.payload, wp.qos_value, wp.props});
        co_await route_publish(wp, client_id);
        logger::info("mqtt will delayed published client={}", client_id);
    }

    auto publish_will(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        if (!conn->session || !conn->session->will_msg) co_return;
        auto& w = *conn->session->will_msg;
        publish_message wp;
        wp.topic = w.topic;   wp.payload = w.message;
        wp.qos_value = w.qos_value; wp.retain = w.retain;
        wp.props = w.props;
        if (wp.retain) retained_.store(wp.topic, retained_message{
            wp.topic, wp.payload, wp.qos_value, wp.props});
        co_await route_publish(wp, conn->session->client_id);
        conn->session->will_msg.reset();
    }

    // =========================================================================
    // 连接清理
    // =========================================================================

    auto cleanup_connection(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        if (!conn->session) co_return;
        auto& cid = conn->session->client_id;
        conn->session->go_offline();
        {
            co_await channels_rw_.lock();
            async_unique_lock_guard wg(channels_rw_, std::adopt_lock);
            online_channels_.erase(cid);
        }
        if (conn->session->clean_session && conn->session->session_expiry_interval == 0) {
            sub_map_.erase_client(cid);
            shared_store_.remove_client(cid);
            sessions_.remove(cid);
        }
    }

    // =========================================================================
    // 消息路由 — 使用 subscription_map trie
    // =========================================================================

    auto route_publish(const publish_message& msg, const std::string& sender_cid)
        -> task<void>
    {
        auto matches = sub_map_.match(msg.topic);

        // 去重: 每个 client 只投递一次
        std::map<std::string, subscribe_entry> targets;
        for (auto& m : matches) {
            if (m.client_id == sender_cid && m.entry.no_local) continue;
            targets.try_emplace(m.client_id, m.entry);
        }

        // 共享订阅
        auto shared_targets = shared_store_.find_matching_groups(
            msg.topic,
            [](std::string_view filter, std::string_view topic) {
                return topic_matches(filter, topic);
            },
            [this](const std::string& cid) -> bool {
                auto* ss = sessions_.find(cid);
                return ss && ss->online;
            }
        );
        for (auto& [cid, filter] : shared_targets) {
            if (cid == sender_cid) continue;
            if (targets.find(cid) == targets.end()) {
                subscribe_entry se;
                se.topic_filter = filter;
                se.max_qos = msg.qos_value;
                targets[cid] = se;
            }
        }

        // 投递
        for (auto& [cid, entry] : targets) {
            auto effective_qos = static_cast<qos>(
                std::min(static_cast<std::uint8_t>(msg.qos_value),
                         static_cast<std::uint8_t>(entry.max_qos)));
            publish_message fwd;
            fwd.topic = msg.topic;  fwd.payload = msg.payload;
            fwd.qos_value = effective_qos; fwd.retain = false;
            fwd.props = msg.props;

            // v5 Subscription Identifier
            if (entry.subscription_id > 0)
                fwd.props.push_back({property_id::subscription_identifier,
                    entry.subscription_id});

            auto* ss = sessions_.find(cid);
            if (!ss) continue;

            if (ss->online) {
                channel<publish_message>* ch = nullptr;
                {
                    co_await channels_rw_.lock_shared();
                    async_shared_lock_guard rg(channels_rw_, std::adopt_lock);
                    auto it = online_channels_.find(cid);
                    if (it != online_channels_.end()) ch = it->second;
                }
                if (ch) {
                    (void)co_await ch->send(std::move(fwd));
                    continue;
                }
            }
            ss->enqueue_offline(std::move(fwd));
        }
    }

    // =========================================================================
    // 保留消息投递
    // =========================================================================

    auto deliver_retained(std::shared_ptr<detail::conn_state> conn,
                          const subscribe_entry& entry,
                          const std::string& actual_filter) -> task<void>
    {
        auto matches = retained_.match(actual_filter);
        for (auto& rm : matches) {
            auto eq = static_cast<qos>(std::min(
                static_cast<std::uint8_t>(rm.qos_value),
                static_cast<std::uint8_t>(entry.max_qos)));
            std::uint16_t pid = 0;
            if (eq != qos::at_most_once && conn->session)
                pid = conn->session->alloc_packet_id();
            (void)co_await conn->do_write(encode_publish(
                rm.topic, rm.payload, eq,
                entry.retain_as_published, false, pid,
                conn->version, rm.props));
        }
    }

    // =========================================================================
    // Phase 2.2: Inflight 重发
    // =========================================================================

    auto deliver_inflight_resend(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        if (!conn->session) co_return;
        for (auto& im : conn->session->inflight_out) {
            if (im.expected_ack == control_packet_type::puback ||
                im.expected_ack == control_packet_type::pubrec) {
                auto wr = co_await conn->do_write(encode_publish(
                    im.msg.topic, im.msg.payload, im.msg.qos_value,
                    false, true, im.packet_id, conn->version, im.msg.props));
                if (!wr) break;
                im.send_time = std::chrono::steady_clock::now();
            } else if (im.expected_ack == control_packet_type::pubcomp) {
                auto wr = co_await conn->do_write(encode_pubrel(im.packet_id, conn->version));
                if (!wr) break;
            }
        }
    }

    // =========================================================================
    // 离线队列投递
    // =========================================================================

    auto deliver_offline_queue(std::shared_ptr<detail::conn_state> conn) -> task<void> {
        if (!conn->session) co_return;
        auto& queue = conn->session->offline_queue;
        for (auto& msg : queue) {
            // 检查 message_expiry_interval 过期
            if (session_state::check_message_expiry(msg)) continue;

            std::uint16_t pid = 0;
            if (msg.qos_value != qos::at_most_once)
                pid = conn->session->alloc_packet_id();
            auto wr = co_await conn->do_write(encode_publish(
                msg.topic, msg.payload, msg.qos_value,
                false, false, pid, conn->version, msg.props));
            if (!wr) break;
            if (msg.qos_value != qos::at_most_once) {
                inflight_message im;
                im.packet_id = pid;  im.msg = msg;
                im.expected_ack = (msg.qos_value == qos::at_least_once)
                    ? control_packet_type::puback : control_packet_type::pubrec;
                im.send_time = std::chrono::steady_clock::now();
                conn->session->inflight_out.push_back(std::move(im));
            }
        }
        queue.clear();
    }

    // =========================================================================
    // 工具
    // =========================================================================

    // =========================================================================
    // 协议错误断连辅助 (v5 发送 DISCONNECT reason code)
    // =========================================================================

    static auto send_disconnect_and_close(
        std::shared_ptr<detail::conn_state> conn,
        std::uint8_t reason_code
    ) -> task<void>
    {
        if (conn->version == protocol_version::v5) {
            auto pkt = encode_disconnect(conn->version, reason_code);
            (void)co_await conn->do_write(pkt);
        }
        conn->connected = false;
        conn->delivery_ch.close();
        conn->sock.close();
    }

    static auto generate_client_id() -> std::string {
        static std::atomic<std::uint64_t> counter{0};
        return "auto-" + std::to_string(
            counter.fetch_add(1, std::memory_order_relaxed));
    }

    // =========================================================================
    // 成员
    // =========================================================================

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

    // async_shared_mutex: 读多写少
    //   读 (route_publish): co_await lock_shared()
    //   写 (connect/disconnect): co_await lock()
    //   stop(): try_lock() 自旋获取写锁
    async_shared_mutex channels_rw_;
    std::map<std::string, channel<publish_message>*> online_channels_;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context>   ssl_ctx_;
    std::unique_ptr<tcp::acceptor> tls_acc_;
#endif
};

} // namespace cnetmod::mqtt