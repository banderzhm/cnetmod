module;

/// cnetmod.protocol.mqtt:broker — MQTT Broker Core Implementation

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

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
import :broker;
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

auto detail::conn_state::do_write(const std::string& data)
    -> task<std::expected<std::size_t, std::error_code>>
{
#ifdef CNETMOD_HAS_SSL
    if (ssl) co_return co_await ssl->async_write(
        const_buffer{data.data(), data.size()});
#endif
    co_return co_await async_write(io, sock,
        const_buffer{data.data(), data.size()});
}

auto detail::conn_state::do_read(mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
#ifdef CNETMOD_HAS_SSL
    if (ssl) co_return co_await ssl->async_read(buf);
#endif
    co_return co_await async_read(io, sock, buf);
}

auto broker::listen(std::string_view host, std::uint16_t port)
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
auto broker::listen_tls(std::string_view host, std::uint16_t port)
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
#endif

auto broker::run() -> task<void> {
    running_ = true;
#ifdef CNETMOD_HAS_SSL
    if (tls_acc_) spawn(ctx_, run_tls_accept());
#endif
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

void broker::stop() {
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

#ifdef CNETMOD_HAS_SSL
auto broker::run_tls_accept() -> task<void> {
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

auto broker::session_expiry_loop() -> task<void> {
    while (running_) {
        co_await async_sleep(ctx_, std::chrono::seconds(60));
        if (!running_) break;

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

auto broker::keep_alive_watchdog(std::shared_ptr<detail::conn_state> conn) -> task<void> {
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

auto broker::read_frame(detail::conn_state& conn)
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

auto broker::handle_connection(socket client, io_context& io, bool use_tls) -> task<void> {
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

    auto frame_r = co_await read_frame(*conn);
    if (!frame_r) { conn->sock.close(); co_return; }

    if (frame_r->type != control_packet_type::connect) {
        conn->sock.close();
        co_return;
    }

    auto ok = co_await handle_connect(*frame_r, conn);
    if (!ok) { conn->sock.close(); co_return; }

    spawn(io, delivery_loop(conn));
    spawn(io, retry_loop(conn));

    if (conn->keep_alive > 0)
        spawn(io, keep_alive_watchdog(conn));

    while (conn->connected) {
        auto fr = co_await read_frame(*conn);
        if (!fr) {
            co_await handle_unexpected_disconnect(conn);
            co_return;
        }

        conn->last_packet_time = std::chrono::steady_clock::now();

        switch (fr->type) {
        case control_packet_type::connect:
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

auto broker::delivery_loop(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    while (conn->connected) {
        auto msg_opt = co_await conn->delivery_ch.receive();
        if (!msg_opt) break;

        if (!conn->connected) break;

        auto& msg = *msg_opt;

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

        if (conn->max_packet_size > 0 && pkt.size() > conn->max_packet_size) {
            logger::debug("mqtt delivery: packet too large for client={}",
                conn->session ? conn->session->client_id : "?");
            continue;
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

auto broker::retry_loop(std::shared_ptr<detail::conn_state> conn) -> task<void> {
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

            auto pkt = encode_publish(
                it->msg.topic, it->msg.payload, it->msg.qos_value,
                false, true, it->packet_id, conn->version, it->msg.props);

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

auto broker::handle_connect(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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
    auto [ss, session_present] = sessions_.create_or_resume(
        cd.client_id, cd.clean_session, cd.version);

    ss.keep_alive = conn->keep_alive;
    ss.will_msg   = cd.will_msg;
    ss.username   = cd.username;

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
            if (p.id == property_id::maximum_packet_size)
                if (auto* v = std::get_if<std::uint32_t>(&p.value))
                    conn->max_packet_size = static_cast<std::size_t>(*v);
        }
        conn->alias_recv.set_max(opts_.topic_alias_maximum);
    }

    conn->session  = &ss;
    conn->connected = true;

    {
        co_await channels_rw_.lock();
        async_unique_lock_guard wg(channels_rw_, std::adopt_lock);
        online_channels_[cd.client_id] = &conn->delivery_ch;
    }

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

    if (cd.version == protocol_version::v5 && opts_.max_keep_alive > 0
        && conn->keep_alive > opts_.max_keep_alive) {
        conn->keep_alive = opts_.max_keep_alive;
        ss.keep_alive = conn->keep_alive;
    }
    properties connack_props;
    if (cd.version == protocol_version::v5) {
        if (cd_client_id_was_empty)
            connack_props.push_back(
                mqtt_property::string_prop(property_id::assigned_client_identifier, cd.client_id));
        if (opts_.topic_alias_maximum > 0)
            connack_props.push_back({property_id::topic_alias_maximum,
                opts_.topic_alias_maximum});
        if (opts_.max_keep_alive > 0 && conn->keep_alive != cd.keep_alive)
            connack_props.push_back({property_id::server_keep_alive, conn->keep_alive});
        if (opts_.receive_maximum < 65535)
            connack_props.push_back({property_id::receive_maximum, opts_.receive_maximum});
        if (opts_.maximum_packet_size > 0)
            connack_props.push_back({property_id::maximum_packet_size, opts_.maximum_packet_size});
        if (opts_.maximum_qos != qos::exactly_once)
            connack_props.push_back(
                mqtt_property::byte_prop(property_id::maximum_qos,
                    static_cast<std::uint8_t>(opts_.maximum_qos)));
        if (!opts_.retain_available)
            connack_props.push_back(
                mqtt_property::byte_prop(property_id::retain_available, 0));
        if (!opts_.wildcard_sub_available)
            connack_props.push_back(
                mqtt_property::byte_prop(property_id::wildcard_subscription_available, 0));
        if (!opts_.sub_id_available)
            connack_props.push_back(
                mqtt_property::byte_prop(property_id::subscription_identifier_available, 0));
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

    if (session_present)
        co_await deliver_inflight_resend(conn);

    co_await deliver_offline_queue(conn);
    co_return true;
}
auto broker::handle_publish(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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

    if (static_cast<std::uint8_t>(msg.qos_value) >
        static_cast<std::uint8_t>(opts_.maximum_qos)) {
        logger::warn("mqtt PUBLISH qos={} exceeds server maximum_qos={}",
            to_string(msg.qos_value), to_string(opts_.maximum_qos));
        co_await send_disconnect_and_close(conn,
            static_cast<std::uint8_t>(v5::disconnect_reason_code::qos_not_supported));
        co_return;
    }

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

    if (security_.enabled() && conn->session)
        if (!security_.authorize_publish(conn->session->username, msg.topic)) {
            logger::warn("mqtt publish denied user={} topic={}",
                conn->session->username, msg.topic);
            co_return;
        }

    if (msg.qos_value == qos::at_least_once && msg.packet_id != 0) {
        (void)co_await conn->do_write(encode_puback(msg.packet_id, conn->version));
    }

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
auto broker::handle_puback(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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

auto broker::handle_pubrec(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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

auto broker::handle_pubrel(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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

auto broker::handle_pubcomp(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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
auto broker::handle_subscribe(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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
auto broker::handle_unsubscribe(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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

auto broker::handle_pingreq(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    (void)co_await conn->do_write(encode_pingresp());
}

auto broker::handle_disconnect(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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

auto broker::handle_auth(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
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
auto broker::handle_unexpected_disconnect(std::shared_ptr<detail::conn_state> conn)
    -> task<void>
{
    conn->connected = false;
    if (conn->session) {
        logger::info("mqtt unexpected disconnect client={}", conn->session->client_id);

        if (conn->session->will_msg) {
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

auto broker::will_delay_task(io_context& io, std::string client_id,
                     will will_msg, std::uint32_t delay_sec) -> task<void>
{
    co_await async_sleep(io, std::chrono::seconds(delay_sec));
    auto* ss = sessions_.find(client_id);
    if (ss && ss->online) co_return;

    publish_message wp;
    wp.topic = will_msg.topic;   wp.payload = will_msg.message;
    wp.qos_value = will_msg.qos_value; wp.retain = will_msg.retain;
    wp.props = will_msg.props;
    if (wp.retain) retained_.store(wp.topic, retained_message{
        wp.topic, wp.payload, wp.qos_value, wp.props});
    co_await route_publish(wp, client_id);
    logger::info("mqtt will delayed published client={}", client_id);
}

auto broker::publish_will(std::shared_ptr<detail::conn_state> conn) -> task<void> {
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

auto broker::cleanup_connection(std::shared_ptr<detail::conn_state> conn) -> task<void> {
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
auto broker::route_publish(const publish_message& msg, const std::string& sender_cid)
    -> task<void>
{
    auto matches = sub_map_.match(msg.topic);

    std::map<std::string, subscribe_entry> targets;
    for (auto& m : matches) {
        if (m.client_id == sender_cid && m.entry.no_local) continue;
        targets.try_emplace(m.client_id, m.entry);
    }

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

    for (auto& [cid, entry] : targets) {
        auto effective_qos = static_cast<qos>(
            std::min(static_cast<std::uint8_t>(msg.qos_value),
                     static_cast<std::uint8_t>(entry.max_qos)));
        publish_message fwd;
        fwd.topic = msg.topic;  fwd.payload = msg.payload;
        fwd.qos_value = effective_qos; fwd.retain = false;
        fwd.props = msg.props;

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

auto broker::deliver_retained(std::shared_ptr<detail::conn_state> conn,
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

auto broker::deliver_inflight_resend(std::shared_ptr<detail::conn_state> conn) -> task<void> {
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

auto broker::deliver_offline_queue(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    if (!conn->session) co_return;
    auto& queue = conn->session->offline_queue;
    for (auto& msg : queue) {
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

auto broker::send_disconnect_and_close(
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

auto broker::generate_client_id() -> std::string {
    static std::atomic<std::uint64_t> counter{0};
    return "auto-" + std::to_string(
        counter.fetch_add(1, std::memory_order_relaxed));
}

} // namespace cnetmod::mqtt
