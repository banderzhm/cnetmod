/// cnetmod.protocol.mqtt:ws_transport — MQTT over WebSocket 传输层
/// 通过 WebSocket binary frames 传输 MQTT 报文
/// 子协议: "mqtt"
/// 集成: subscription_map trie, shared_sub, security/ACL, topic_alias, will_delay

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:ws_transport;

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
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;
import cnetmod.protocol.websocket;
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
// WebSocket Broker 配置
// =============================================================================

export struct ws_broker_options {
    std::uint16_t port                   = 8083;   // 默认 MQTT over WS 端口
    std::string   host                   = "*******";
    std::string   path                   = "/mqtt"; // WebSocket 路径
    std::uint16_t max_connections        = 10000;
    std::uint32_t default_session_expiry = 0;
    std::uint16_t max_keep_alive         = 600;    // 最大 keep-alive 秒数
    std::size_t   delivery_channel_size  = 1000;   // 投递 channel 容量
    std::uint16_t topic_alias_maximum    = 0;       // 服务端支持的 topic alias 最大数

    // v5 能力通告
    std::uint16_t receive_maximum        = 65535;
    std::uint32_t maximum_packet_size    = 0;       // 0=无限制
    qos           maximum_qos            = qos::exactly_once;
    bool          retain_available       = true;
    bool          wildcard_sub_available = true;
    bool          sub_id_available       = true;
    bool          shared_sub_available   = true;
};

// =============================================================================
// MQTT over WebSocket Broker
// =============================================================================

export class ws_broker {
public:
    /// 单线程模式
    explicit ws_broker(io_context& ctx) : ctx_(ctx), ws_server_(ctx) {}

    /// 多核模式
    explicit ws_broker(server_context& sctx)
        : ctx_(sctx.accept_io()), ws_server_(sctx) {}

    /// 配置
    void set_options(ws_broker_options opts) { opts_ = std::move(opts); }
    void set_security(security_config cfg) { security_ = std::move(cfg); }
    void set_auth_handler(broker_auth_handler h) { auth_handler_ = std::move(h); }

    /// 获取引用
    [[nodiscard]] auto security() noexcept -> security_config& { return security_; }
    [[nodiscard]] auto sessions() noexcept -> session_store& { return sessions_; }
    [[nodiscard]] auto sessions() const noexcept -> const session_store& { return sessions_; }
    [[nodiscard]] auto retained() noexcept -> retained_store& { return retained_; }
    [[nodiscard]] auto retained() const noexcept -> const retained_store& { return retained_; }
    [[nodiscard]] auto subscriptions() noexcept -> subscription_map& { return sub_map_; }

    /// 监听端口
    auto listen() -> std::expected<void, std::error_code> {
        return listen(opts_.host, opts_.port);
    }

    auto listen(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>
    {
        // 注册 WebSocket 路由
        ws_server_.on(opts_.path, [this](ws::ws_context& ctx) -> task<void> {
            co_await handle_ws_connection(ctx);
        });

        logger::info("mqtt ws_broker listening on {}:{}{}", host, port, opts_.path);
        return ws_server_.listen(host, port);
    }

    /// 运行
    auto run() -> task<void> {
        co_await ws_server_.run();
    }

    /// 停止
    void stop() {
        ws_server_.stop();
        std::lock_guard lock(channels_mtx_);
        for (auto& [cid, ch] : online_channels_) ch->close();
        online_channels_.clear();
        logger::info("mqtt ws_broker stopped");
    }

private:
    // =========================================================================
    // WebSocket 连接处理
    // =========================================================================

    auto handle_ws_connection(ws::ws_context& wctx) -> task<void> {
        mqtt_parser parser;
        session_state* session = nullptr;
        protocol_version version = protocol_version::v3_1_1;
        bool connected = false;
        topic_alias_recv alias_recv{0};
        std::size_t max_packet_size = 0;
        channel<publish_message> delivery_ch(opts_.delivery_channel_size);

        // 等待 CONNECT 报文
        auto frame_r = co_await ws_read_frame(wctx, parser);
        if (!frame_r) co_return;

        auto& frame = *frame_r;
        if (frame.type != control_packet_type::connect) co_return;

        // 处理 CONNECT
        auto cd_r = decode_connect(frame.payload);
        if (!cd_r) {
            logger::debug("mqtt ws: decode CONNECT failed");
            auto pkt = encode_connack(false,
                static_cast<std::uint8_t>(connect_return_code::unacceptable_protocol_version),
                protocol_version::v3_1_1);
            co_await ws_write(wctx, pkt);
            co_return;
        }

        auto& cd = *cd_r;
        version = cd.version;

        // 安全认证
        if (security_.enabled()) {
            auto auth_user = security_.authenticate(cd.username, cd.password);
            if (!auth_user) {
                logger::warn("mqtt ws auth failed user={}", cd.username);
                auto rc = (cd.version == protocol_version::v5)
                    ? static_cast<std::uint8_t>(v5::connect_reason_code::bad_user_name_or_password)
                    : static_cast<std::uint8_t>(connect_return_code::bad_user_name_or_password);
                co_await ws_write(wctx, encode_connack(false, rc, cd.version));
                co_return;
            }
            cd.username = *auth_user;
        }

        // 生成 client_id
        bool cd_client_id_was_empty = cd.client_id.empty();
        if (cd.client_id.empty()) {
            if (cd.clean_session) {
                cd.client_id = generate_client_id();
            } else {
                auto rc = (cd.version == protocol_version::v5)
                    ? static_cast<std::uint8_t>(v5::connect_reason_code::client_identifier_not_valid)
                    : static_cast<std::uint8_t>(connect_return_code::identifier_rejected);
                co_await ws_write(wctx, encode_connack(false, rc, cd.version));
                co_return;
            }
        }

        // Session takeover
        auto* existing = sessions_.find(cd.client_id);
        if (existing && existing->online) {
            logger::info("mqtt ws session takeover client={}", cd.client_id);
            existing->go_offline();
            std::lock_guard lock(channels_mtx_);
            auto it = online_channels_.find(cd.client_id);
            if (it != online_channels_.end()) {
                it->second->close();
                online_channels_.erase(it);
            }
        }

        // 创建或恢复会话
        auto [ss, session_present] = sessions_.create_or_resume(
            cd.client_id, cd.clean_session, cd.version);
        ss.keep_alive = cd.keep_alive;
        ss.will_msg = cd.will_msg;
        ss.username = cd.username;

        // v5 属性
        if (cd.version == protocol_version::v5) {
            for (auto& p : cd.props) {
                if (p.id == property_id::session_expiry_interval)
                    if (auto* val = std::get_if<std::uint32_t>(&p.value))
                        ss.session_expiry_interval = *val;
                if (p.id == property_id::receive_maximum)
                    if (auto* v = std::get_if<std::uint16_t>(&p.value)) {
                        ss.receive_maximum = *v;
                        ss.inflight_quota  = *v;
                    }
                // 客户端 topic_alias_maximum → 服务端可向该客户端发送的别名上限 (暂不使用)
                if (p.id == property_id::maximum_packet_size)
                    if (auto* v = std::get_if<std::uint32_t>(&p.value))
                        max_packet_size = static_cast<std::size_t>(*v);
            }
            // 服务端 opts_.topic_alias_maximum → 服务端可接收的别名上限 (已在 CONNACK 通告)
            alias_recv.set_max(opts_.topic_alias_maximum);
        }

        session = &ss;
        connected = true;

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
        std::uint16_t original_keep_alive = cd.keep_alive;
        if (cd.version == protocol_version::v5 && opts_.max_keep_alive > 0
            && cd.keep_alive > opts_.max_keep_alive) {
            ss.keep_alive = opts_.max_keep_alive;
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
            if (opts_.max_keep_alive > 0 && ss.keep_alive != original_keep_alive)
                connack_props.push_back({property_id::server_keep_alive, ss.keep_alive});
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

        co_await ws_write(wctx, encode_connack(session_present, rc, cd.version, connack_props));

        logger::info("mqtt ws connected client={} version={} session_present={}",
            cd.client_id, to_string(cd.version), session_present);

        // 注册在线 channel
        {
            std::lock_guard lock(channels_mtx_);
            online_channels_[cd.client_id] = &delivery_ch;
        }

        // 启动 delivery_loop
        spawn(ctx_, ws_delivery_loop(wctx, session, version, max_packet_size, delivery_ch));

        // Inflight 重发
        if (session_present) {
            for (auto& im : session->inflight_out) {
                if (im.expected_ack == control_packet_type::puback ||
                    im.expected_ack == control_packet_type::pubrec) {
                    co_await ws_write(wctx, encode_publish(
                        im.msg.topic, im.msg.payload, im.msg.qos_value,
                        false, true, im.packet_id, version, im.msg.props));
                    im.send_time = std::chrono::steady_clock::now();
                } else if (im.expected_ack == control_packet_type::pubcomp) {
                    co_await ws_write(wctx, encode_pubrel(im.packet_id, version));
                }
            }
        }

        // 投递离线消息
        for (auto& msg : session->offline_queue) {
            // 检查 message_expiry_interval 过期
            if (session_state::check_message_expiry(msg)) continue;

            std::uint16_t pid = 0;
            if (msg.qos_value != qos::at_most_once)
                pid = session->alloc_packet_id();
            auto pkt = encode_publish(
                msg.topic, msg.payload, msg.qos_value,
                false, false, pid, version, msg.props);
            if (max_packet_size > 0 && pkt.size() > max_packet_size) continue;
            co_await ws_write(wctx, pkt);
            if (msg.qos_value != qos::at_most_once) {
                inflight_message im;
                im.packet_id = pid;  im.msg = msg;
                im.expected_ack = (msg.qos_value == qos::at_least_once)
                    ? control_packet_type::puback : control_packet_type::pubrec;
                im.send_time = std::chrono::steady_clock::now();
                session->inflight_out.push_back(std::move(im));
            }
        }
        session->offline_queue.clear();

        // 主循环
        while (connected && wctx.is_open()) {
            auto fr = co_await ws_read_frame(wctx, parser);
            if (!fr) {
                // 非预期断连
                if (session) {
                    logger::info("mqtt ws unexpected disconnect client={}", session->client_id);

                    if (session->will_msg) {
                        // Will Delay Interval
                        std::uint32_t will_delay = 0;
                        for (auto& p : session->will_msg->props)
                            if (p.id == property_id::will_delay_interval)
                                if (auto* v = std::get_if<std::uint32_t>(&p.value))
                                    will_delay = *v;

                        if (will_delay > 0) {
                            auto cid = session->client_id;
                            auto will_copy = *session->will_msg;
                            session->will_msg.reset();
                            spawn(ctx_, will_delay_task(cid, std::move(will_copy), will_delay));
                        } else {
                            publish_message wp;
                            wp.topic = session->will_msg->topic;
                            wp.payload = session->will_msg->message;
                            wp.qos_value = session->will_msg->qos_value;
                            wp.retain = session->will_msg->retain;
                            wp.props = session->will_msg->props;
                            if (wp.retain)
                                retained_.store(wp.topic, retained_message{
                                    wp.topic, wp.payload, wp.qos_value, wp.props});
                            co_await route_publish(wp, session->client_id);
                            session->will_msg.reset();
                        }
                    }
                    cleanup_session(session);
                }
                delivery_ch.close();
                co_return;
            }

            auto& f = *fr;
            switch (f.type) {
            case control_packet_type::publish: {
                auto msg_r = decode_publish(f.payload, f.flags, version);
                if (!msg_r) break;
                auto& msg = *msg_r;

                // v5 Topic Alias
                if (version == protocol_version::v5) {
                    std::uint16_t alias = 0;
                    for (auto& p : msg.props)
                        if (p.id == property_id::topic_alias)
                            if (auto* v = std::get_if<std::uint16_t>(&p.value))
                                alias = *v;
                    if (alias != 0) {
                        auto resolved = alias_recv.resolve(msg.topic, alias);
                        if (resolved.empty()) {
                            logger::warn("mqtt ws invalid topic alias={}", alias);
                            break;
                        }
                        msg.topic = std::move(resolved);
                    }
                }

                if (!validate_topic_name(msg.topic)) break;

                // ACL 检查
                if (security_.enabled() && session)
                    if (!security_.authorize_publish(session->username, msg.topic)) {
                        logger::warn("mqtt ws publish denied user={} topic={}",
                            session->username, msg.topic);
                        break;
                    }

                // QoS 1
                if (msg.qos_value == qos::at_least_once && msg.packet_id != 0)
                    co_await ws_write(wctx, encode_puback(msg.packet_id, version));

                // QoS 2 → PUBREC, 暂存消息等 PUBREL 后转发
                if (msg.qos_value == qos::exactly_once && msg.packet_id != 0) {
                    if (session) {
                        session->qos2_received.insert(msg.packet_id);
                        session->qos2_pending_publish[msg.packet_id] = msg;
                    }
                    co_await ws_write(wctx, encode_pubrec(msg.packet_id, version));
                    break;
                }

                if (msg.retain)
                    retained_.store(msg.topic, retained_message{
                        msg.topic, msg.payload, msg.qos_value, msg.props});

                co_await route_publish(msg, session ? session->client_id : "");
                break;
            }

            case control_packet_type::puback:
            case control_packet_type::pubcomp: {
                auto ack_r = decode_ack(f.payload, version);
                if (ack_r && session) {
                    std::erase_if(session->inflight_out, [&](const inflight_message& im) {
                        return im.packet_id == ack_r->packet_id;
                    });
                }
                break;
            }

            case control_packet_type::pubrec: {
                auto ack_r = decode_ack(f.payload, version);
                if (ack_r) {
                    co_await ws_write(wctx, encode_pubrel(ack_r->packet_id, version));
                    if (session)
                        for (auto& im : session->inflight_out)
                            if (im.packet_id == ack_r->packet_id) {
                                im.expected_ack = control_packet_type::pubcomp;
                                break;
                            }
                }
                break;
            }

            case control_packet_type::pubrel: {
                auto ack_r = decode_ack(f.payload, version);
                if (ack_r) {
                    co_await ws_write(wctx, encode_pubcomp(ack_r->packet_id, version));
                    if (session) {
                        session->qos2_received.erase(ack_r->packet_id);
                        // 取出暂存的 QoS 2 消息并转发
                        auto it = session->qos2_pending_publish.find(ack_r->packet_id);
                        if (it != session->qos2_pending_publish.end()) {
                            auto pending_msg = std::move(it->second);
                            session->qos2_pending_publish.erase(it);
                            if (pending_msg.retain)
                                retained_.store(pending_msg.topic, retained_message{
                                    pending_msg.topic, pending_msg.payload,
                                    pending_msg.qos_value, pending_msg.props});
                            co_await route_publish(pending_msg, session->client_id);
                        }
                    }
                }
                break;
            }

            case control_packet_type::subscribe: {
                auto sub_r = decode_subscribe(f.payload, version);
                if (!sub_r || !session) break;

                std::vector<std::uint8_t> return_codes;
                for (auto& entry : sub_r->entries) {
                    if (!validate_topic_filter(entry.topic_filter)) {
                        return_codes.push_back(version == protocol_version::v5
                            ? static_cast<std::uint8_t>(v5::suback_reason_code::topic_filter_invalid)
                            : static_cast<std::uint8_t>(suback_return_code::failure));
                        continue;
                    }

                    // ACL 检查
                    if (security_.enabled()) {
                        auto actual_filter = extract_topic_filter(entry.topic_filter);
                        if (!security_.authorize_subscribe(session->username, actual_filter)) {
                            logger::warn("mqtt ws subscribe denied user={} filter={}",
                                session->username, entry.topic_filter);
                            return_codes.push_back(version == protocol_version::v5
                                ? static_cast<std::uint8_t>(v5::suback_reason_code::not_authorized)
                                : static_cast<std::uint8_t>(suback_return_code::failure));
                            continue;
                        }
                    }

                    bool is_new = session->add_subscription(entry);

                    // 注册到 trie + shared store
                    auto shared = parse_shared_subscription(entry.topic_filter);
                    if (shared && !shared->share_name.empty()) {
                        sub_map_.insert(shared->topic_filter, session->client_id, entry);
                        shared_store_.add_member(shared->share_name,
                            shared->topic_filter, session->client_id);
                    } else {
                        sub_map_.insert(entry.topic_filter, session->client_id, entry);
                    }

                    return_codes.push_back(static_cast<std::uint8_t>(entry.max_qos));

                    // 投递保留消息
                    if (is_new || entry.rh == retain_handling::send) {
                        auto actual = extract_topic_filter(entry.topic_filter);
                        auto matches = retained_.match(actual);
                        for (auto& rm : matches) {
                            auto eff_qos = static_cast<qos>(
                                std::min(static_cast<std::uint8_t>(rm.qos_value),
                                         static_cast<std::uint8_t>(entry.max_qos)));
                            std::uint16_t pid = 0;
                            if (eff_qos != qos::at_most_once)
                                pid = session->alloc_packet_id();
                            co_await ws_write(wctx, encode_publish(
                                rm.topic, rm.payload, eff_qos,
                                entry.retain_as_published, false, pid, version, rm.props));
                        }
                    }
                }

                co_await ws_write(wctx,
                    encode_suback(sub_r->packet_id, return_codes, version, sub_r->props));
                break;
            }

            case control_packet_type::unsubscribe: {
                auto unsub_r = decode_unsubscribe(f.payload, version);
                if (!unsub_r || !session) break;

                std::vector<std::uint8_t> reason_codes;
                for (auto& tf : unsub_r->topic_filters) {
                    bool existed = session->remove_subscription(tf);
                    auto shared = parse_shared_subscription(tf);
                    if (shared && !shared->share_name.empty()) {
                        sub_map_.erase(shared->topic_filter, session->client_id);
                        shared_store_.remove_member(shared->share_name,
                            shared->topic_filter, session->client_id);
                    } else {
                        sub_map_.erase(tf, session->client_id);
                    }
                    if (version == protocol_version::v5)
                        reason_codes.push_back(existed
                            ? static_cast<std::uint8_t>(v5::unsuback_reason_code::success)
                            : static_cast<std::uint8_t>(v5::unsuback_reason_code::no_subscription_existed));
                }
                co_await ws_write(wctx,
                    encode_unsuback(unsub_r->packet_id, version, reason_codes));
                break;
            }

            case control_packet_type::pingreq:
                co_await ws_write(wctx, encode_pingresp());
                break;

            case control_packet_type::disconnect: {
                connected = false;
                if (session) {
                    session->will_msg.reset();
                    if (version == protocol_version::v5 && !f.payload.empty()) {
                        auto dc = decode_disconnect(f.payload, version);
                        for (auto& p : dc.props)
                            if (p.id == property_id::session_expiry_interval)
                                if (auto* val = std::get_if<std::uint32_t>(&p.value))
                                    session->session_expiry_interval = *val;
                    }
                    cleanup_session(session);
                }
                delivery_ch.close();
                break;
            }

            case control_packet_type::auth: {
                if (version == protocol_version::v5 && session && auth_handler_) {
                    auto auth_r = decode_auth(f.payload);
                    auto resp = auth_handler_(
                        session->client_id, auth_r.reason_code, auth_r.props);
                    if (resp)
                        co_await ws_write(wctx, encode_auth(resp->first, resp->second));
                }
                break;
            }

            default:
                break;
            }
        }
    }

    // =========================================================================
    // WebSocket 传输辅助
    // =========================================================================

    static auto ws_read_frame(ws::ws_context& wctx, mqtt_parser& parser)
        -> task<std::expected<mqtt_frame, std::string>>
    {
        while (true) {
            auto frame = parser.next();
            if (frame) co_return std::move(*frame);

            auto msg_r = co_await wctx.recv();
            if (!msg_r)
                co_return std::unexpected(std::string("ws recv failed"));

            auto& ws_msg = *msg_r;
            if (ws_msg.op == ws::opcode::close)
                co_return std::unexpected(std::string("ws closed"));

            parser.feed(std::string_view(
                reinterpret_cast<const char*>(ws_msg.payload.data()),
                ws_msg.payload.size()));
        }
    }

    static auto ws_write(ws::ws_context& wctx, const std::string& data)
        -> task<void>
    {
        auto span = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(data.data()),
            data.size());
        (void)co_await wctx.send_binary(span);
    }

    // =========================================================================
    // delivery_loop — 从 channel 读取并写入 WebSocket
    // =========================================================================

    auto ws_delivery_loop(ws::ws_context& wctx, session_state* session,
                          protocol_version version, std::size_t max_packet_size,
                          channel<publish_message>& ch) -> task<void>
    {
        while (true) {
            auto msg_opt = co_await ch.receive();
            if (!msg_opt) break;

            auto& msg = *msg_opt;

            // Receive Maximum 流控
            if (msg.qos_value != qos::at_most_once && session) {
                if (session->inflight_out.size() >=
                    static_cast<std::size_t>(session->receive_maximum)) {
                    session->enqueue_offline(std::move(msg));
                    continue;
                }
            }

            std::uint16_t pid = 0;
            if (msg.qos_value != qos::at_most_once && session)
                pid = session->alloc_packet_id();

            auto pkt = encode_publish(
                msg.topic, msg.payload, msg.qos_value,
                false, false, pid, version, msg.props);

            // Maximum Packet Size 检查
            if (max_packet_size > 0 && pkt.size() > max_packet_size) continue;

            co_await ws_write(wctx, pkt);

            if (msg.qos_value != qos::at_most_once && session) {
                inflight_message im;
                im.packet_id    = pid;
                im.msg          = msg;
                im.expected_ack = (msg.qos_value == qos::at_least_once)
                    ? control_packet_type::puback : control_packet_type::pubrec;
                im.send_time    = std::chrono::steady_clock::now();
                session->inflight_out.push_back(std::move(im));
            }
        }
    }

    // =========================================================================
    // 消息路由 — subscription_map trie
    // =========================================================================

    auto route_publish(const publish_message& msg,
                       const std::string& sender_cid) -> task<void>
    {
        auto matches = sub_map_.match(msg.topic);

        // 去重
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
                    std::lock_guard lock(channels_mtx_);
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
    // Will Delay
    // =========================================================================

    auto will_delay_task(std::string client_id, will will_msg,
                         std::uint32_t delay_sec) -> task<void>
    {
        co_await async_sleep(ctx_, std::chrono::seconds(delay_sec));
        auto* ss = sessions_.find(client_id);
        if (ss && ss->online) co_return; // 已重连

        publish_message wp;
        wp.topic = will_msg.topic;   wp.payload = will_msg.message;
        wp.qos_value = will_msg.qos_value; wp.retain = will_msg.retain;
        wp.props = will_msg.props;
        if (wp.retain) retained_.store(wp.topic, retained_message{
            wp.topic, wp.payload, wp.qos_value, wp.props});
        co_await route_publish(wp, client_id);
        logger::info("mqtt ws will delayed published client={}", client_id);
    }

    // =========================================================================
    // 连接清理
    // =========================================================================

    void cleanup_session(session_state* session) {
        if (!session) return;
        auto& cid = session->client_id;
        session->go_offline();
        {
            std::lock_guard lock(channels_mtx_);
            online_channels_.erase(cid);
        }
        if (session->clean_session && session->session_expiry_interval == 0) {
            sub_map_.erase_client(cid);
            shared_store_.remove_client(cid);
            sessions_.remove(cid);
        }
    }

    // =========================================================================
    // 工具
    // =========================================================================

    static auto generate_client_id() -> std::string {
        static std::atomic<std::uint64_t> counter{0};
        auto c = counter.fetch_add(1, std::memory_order_relaxed);
        return "ws-auto-" + std::to_string(c);
    }

    // =========================================================================
    // 成员
    // =========================================================================

    io_context&          ctx_;
    ws::server           ws_server_;
    ws_broker_options    opts_;
    session_store        sessions_;
    retained_store       retained_;
    subscription_map     sub_map_;
    shared_target_store  shared_store_;
    security_config      security_;
    broker_auth_handler  auth_handler_;

    // 在线投递 channel 管理
    std::mutex                                      channels_mtx_;
    std::map<std::string, channel<publish_message>*> online_channels_;
};

} // namespace cnetmod::mqtt
