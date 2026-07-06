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
import cnetmod.coro.mutex;
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
import :persistence;

namespace cnetmod::mqtt {

using detail::outbound_priority;

namespace {

void flush_cross_worker_delivery(void* arg);

void finish_cross_worker_delivery(const std::shared_ptr<detail::conn_state>& target) {
    if (!target || !target->connected) {
        if (target) {
            std::scoped_lock lock(target->cross_delivery_mtx);
            target->cross_delivery_pending.clear();
            target->cross_delivery_scheduled = false;
        }
        return;
    }

    bool should_reschedule = false;
    {
        std::scoped_lock lock(target->cross_delivery_mtx);
        if (target->cross_delivery_pending.empty()) {
            target->cross_delivery_scheduled = false;
        } else {
            should_reschedule = true;
        }
    }
    if (should_reschedule) {
        auto* next_box = new std::shared_ptr<detail::conn_state>(target);
        target->io.post(flush_cross_worker_delivery, next_box);
    }
}

auto deliver_on_target_batch(std::shared_ptr<detail::conn_state> target,
                             std::deque<detail::cross_delivery_item> batch) -> task<void> {
    for (auto& item : batch) {
        if (target && target->connected) {
            (void)co_await target->delivery_ch.send(std::move(item.msg));
        }
    }
    finish_cross_worker_delivery(target);
}

void flush_cross_worker_delivery(void* arg) {
    auto target_box = std::unique_ptr<std::shared_ptr<detail::conn_state>>(
        static_cast<std::shared_ptr<detail::conn_state>*>(arg));
    auto target = std::move(*target_box);
    std::deque<detail::cross_delivery_item> batch;
    constexpr std::size_t max_flush_items = 1024;

    if (target && target->connected) {
        std::scoped_lock lock(target->cross_delivery_mtx);
        while (!target->cross_delivery_pending.empty() &&
               batch.size() < max_flush_items) {
            batch.push_back(std::move(target->cross_delivery_pending.front()));
            target->cross_delivery_pending.pop_front();
        }
        if (batch.empty()) {
            target->cross_delivery_scheduled = false;
            return;
        }
    }

    if (target && target->connected) {
        std::array<publish_message*, max_flush_items> messages{};
        std::size_t message_count = 0;
        for (auto& item : batch) {
            messages[message_count++] = &item.msg;
        }

        const auto sent = target->delivery_ch.try_send_many_move(
            std::span<publish_message*>{messages.data(), message_count});

        if (sent < batch.size()) {
            std::deque<detail::cross_delivery_item> remaining;
            for (std::size_t i = sent; i < batch.size(); ++i) {
                remaining.push_back(std::move(batch[i]));
            }
            spawn(target->io, deliver_on_target_batch(target, std::move(remaining)));
            return;
        }
    }

    finish_cross_worker_delivery(target);
}

auto deliver_to_connection(io_context& origin,
                           const std::shared_ptr<detail::conn_state>& target,
                           publish_message msg) -> task<bool> {
    (void)origin;
    if (!target || !target->connected) co_return false;

    bool should_schedule = false;
    {
        std::scoped_lock lock(target->cross_delivery_mtx);
        target->cross_delivery_pending.push_back(detail::cross_delivery_item{
            .msg = std::move(msg),
        });
        if (!target->cross_delivery_scheduled) {
            target->cross_delivery_scheduled = true;
            should_schedule = true;
        }
    }
    if (should_schedule) {
        auto* target_box = new std::shared_ptr<detail::conn_state>(target);
        target->io.post(flush_cross_worker_delivery, target_box);
    }
    co_return true;
}

} // namespace

auto detail::conn_state::do_write(std::string data,
                                  outbound_priority priority,
                                  bool batchable)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto ticket_r = co_await submit_write(std::move(data), priority, batchable);
    if (!ticket_r) co_return std::unexpected(ticket_r.error());

    auto result = co_await ticket_r->completion->receive();
    if (!result) {
        co_return std::unexpected(
            std::make_error_code(std::errc::broken_pipe));
    }
    co_return *result;
}

auto detail::conn_state::submit_write(std::string data,
                                      outbound_priority priority,
                                      bool batchable)
    -> task<std::expected<write_ticket, std::error_code>>
{
    auto size = data.size();
    auto completion = std::make_shared<
        channel<std::expected<std::size_t, std::error_code>>>(1);

    auto ok = co_await write_ch.send(detail::outbound_packet{
        .data = std::move(data),
        .priority = priority,
        .batchable = batchable,
        .logical_size = size,
        .completion = completion,
    });
    if (!ok) {
        co_return std::unexpected(
            std::make_error_code(std::errc::broken_pipe));
    }
    co_return write_ticket{
        .logical_size = size,
        .completion = std::move(completion),
    };
}

auto detail::conn_state::submit_write_queued(std::string data,
                                             outbound_priority priority,
                                             bool batchable,
                                             std::size_t logical_messages)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto size = data.size();
    auto ok = co_await write_ch.send(detail::outbound_packet{
        .data = std::move(data),
        .priority = priority,
        .batchable = batchable,
        .logical_size = logical_messages == 0 ? 1 : logical_messages,
        .completion = {},
    });
    if (!ok) {
        co_return std::unexpected(
            std::make_error_code(std::errc::broken_pipe));
    }
    co_return size;
}

auto detail::conn_state::raw_write(const std::string& data)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_await write_mtx.lock();
    async_lock_guard wg(write_mtx, std::adopt_lock);
#ifdef CNETMOD_HAS_SSL
    if (ssl) {
        auto r = co_await ssl->async_write_all(
            const_buffer{data.data(), data.size()});
        if (!r) co_return std::unexpected(r.error());
        co_return data.size();
    }
#endif
    auto r = co_await async_write_all(io, sock,
        const_buffer{data.data(), data.size()});
    if (!r) co_return std::unexpected(r.error());
    co_return data.size();
}

auto detail::conn_state::do_read(mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
#ifdef CNETMOD_HAS_SSL
    if (ssl) co_return co_await ssl->async_read(buf);
#endif
    co_return co_await async_read(io, sock, buf);
}

auto broker::metrics() const noexcept -> broker_metrics_snapshot {
    return broker_metrics_snapshot{
        .accepted_connections = accepted_connections_.load(std::memory_order_relaxed),
        .active_connections = active_connections_.load(std::memory_order_relaxed),
        .routed_messages = routed_messages_.load(std::memory_order_relaxed),
        .delivered_messages = delivered_messages_.load(std::memory_order_relaxed),
        .offline_enqueued_messages = offline_enqueued_messages_.load(std::memory_order_relaxed),
        .write_failures = write_failures_.load(std::memory_order_relaxed),
        .protocol_errors = protocol_errors_.load(std::memory_order_relaxed),
        .socket_writes = socket_writes_.load(std::memory_order_relaxed),
        .socket_write_bytes = socket_write_bytes_.load(std::memory_order_relaxed),
        .batch_writes = batch_writes_.load(std::memory_order_relaxed),
        .batch_messages = batch_messages_.load(std::memory_order_relaxed),
    };
}

void broker::invalidate_route_cache() noexcept {
    std::atomic_store_explicit(
        &route_cache_,
        std::shared_ptr<const route_cache_map>{std::make_shared<route_cache_map>()},
        std::memory_order_release);
}

auto broker::listen(std::string_view host, std::uint16_t port, socket_options sock_opts)
    -> std::expected<void, std::error_code>
{
    auto addr_r = ip_address::from_string(host);
    if (!addr_r) return std::unexpected(addr_r.error());
    acc_ = std::make_unique<tcp::acceptor>(ctx_);
    sock_opts.reuse_address = true;
    sock_opts.non_blocking = true;
    auto r = acc_->open(endpoint{*addr_r, port}, sock_opts);
    if (!r) return std::unexpected(r.error());
    logger::info("mqtt broker listening on {}:{}", host, port);
    return {};
}

#ifdef CNETMOD_HAS_SSL
auto broker::listen_tls(std::string_view host, std::uint16_t port, socket_options sock_opts)
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
    sock_opts.reuse_address = true;
    sock_opts.non_blocking = true;
    auto r = tls_acc_->open(endpoint{*addr_r, port}, sock_opts);
    if (!r) return std::unexpected(r.error());
    logger::info("mqtt broker TLS listening on {}:{}", host, port);
    return {};
}
#endif

auto broker::run() -> task<void> {
    running_ = true;
    load_persistence_once();
    if (opts_.persistence_enabled) {
        spawn(ctx_, persistence_flush_loop());
    }
#ifdef CNETMOD_HAS_SSL
    if (tls_acc_) spawn(ctx_, run_tls_accept());
#endif
    spawn(ctx_, session_expiry_loop());

    if (!acc_) {
        while (running_) {
            co_await async_sleep(ctx_, std::chrono::milliseconds{100});
        }
        co_return;
    }

    while (running_) {
        auto r = co_await async_accept(ctx_, acc_->native_socket());
        if (!r) { if (!running_) break; continue; }
        if (opts_.max_connections > 0 &&
            active_connections_.load(std::memory_order_relaxed) >= opts_.max_connections) {
            r->close();
            continue;
        }

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
    for (auto& [cid, target] : online_channels_) {
        if (auto conn = target.conn.lock()) {
            conn->delivery_ch.close();
            conn->write_ch.close();
            conn->write_wake_ch.close();
            conn->sock.close();
        }
    }
    online_channels_.clear();
    channels_rw_.unlock();
    flush_persistence_blocking();
    logger::info("mqtt broker stopped");
}

#ifdef CNETMOD_HAS_SSL
auto broker::run_tls_accept() -> task<void> {
    while (running_) {
        auto r = co_await async_accept(ctx_, tls_acc_->native_socket());
        if (!r) { if (!running_) break; continue; }
        if (opts_.max_connections > 0 &&
            active_connections_.load(std::memory_order_relaxed) >= opts_.max_connections) {
            r->close();
            continue;
        }
        if (sctx_) {
            auto& worker = sctx_->next_worker_io();
            spawn_on(worker, handle_connection(std::move(*r), worker, true));
        } else {
            spawn(ctx_, handle_connection(std::move(*r), ctx_, true));
        }
    }
}
#endif

void broker::load_persistence_once() {
    if (!opts_.persistence_enabled || persistence_loaded_) return;
    persistence_loaded_ = true;

    persistence store(opts_.persistence);

    auto sessions_r = store.load_sessions();
    if (sessions_r) {
        sessions_ = std::move(*sessions_r);
        sub_map_ = subscription_map{};
        shared_store_ = shared_target_store{};
        invalidate_route_cache();

        sessions_.for_each([&](session_state& ss) {
            ss.online = false;
            ss.disconnect_time = std::chrono::steady_clock::now();
            ss.max_offline_queue = opts_.max_offline_queue;
            for (auto& [filter, entry] : ss.subscriptions) {
                auto shared = parse_shared_subscription(filter);
                if (shared && !shared->share_name.empty()) {
                    shared_subscriptions_present_.store(true, std::memory_order_release);
                    sub_map_.insert(shared->topic_filter, ss.client_id, entry);
                    shared_store_.add_member(shared->share_name,
                        shared->topic_filter, ss.client_id);
                } else {
                    sub_map_.insert(filter, ss.client_id, entry);
                }
            }
        });
        logger::info("mqtt persistence loaded sessions={}", sessions_.size());
    } else if (!sessions_r.error().starts_with("file not found:")) {
        logger::warn("mqtt persistence load sessions failed: {}", sessions_r.error());
    }

    auto retained_r = store.load_retained();
    if (retained_r) {
        retained_ = std::move(*retained_r);
        logger::info("mqtt persistence loaded retained={}", retained_.size());
    } else if (!retained_r.error().starts_with("file not found:")) {
        logger::warn("mqtt persistence load retained failed: {}", retained_r.error());
    }
}

auto broker::persistence_flush_loop() -> task<void> {
    persistence store(opts_.persistence);
    while (running_) {
        co_await async_sleep(ctx_, store.options().flush_interval);
        if (!running_) break;

        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        auto sr = store.save_sessions(sessions_);
        if (!sr) logger::warn("mqtt persistence save sessions failed: {}", sr.error());
        auto rr = store.save_retained(retained_);
        if (!rr) logger::warn("mqtt persistence save retained failed: {}", rr.error());
    }
}

void broker::flush_persistence_blocking() {
    if (!opts_.persistence_enabled || !persistence_loaded_) return;
    while (!state_mtx_.try_lock()) {
        std::this_thread::yield();
    }
    async_lock_guard sg(state_mtx_, std::adopt_lock);

    persistence store(opts_.persistence);
    auto sr = store.save_sessions(sessions_);
    if (!sr) logger::warn("mqtt persistence save sessions failed: {}", sr.error());
    auto rr = store.save_retained(retained_);
    if (!rr) logger::warn("mqtt persistence save retained failed: {}", rr.error());
}

auto broker::session_expiry_loop() -> task<void> {
    while (running_) {
        co_await async_sleep(ctx_, std::chrono::seconds(60));
        if (!running_) break;

        std::vector<std::string> expired;
        {
            co_await state_mtx_.lock();
            async_lock_guard sg(state_mtx_, std::adopt_lock);
            sessions_.for_each([&](const session_state& ss) {
                if (ss.is_expired())
                    expired.push_back(ss.client_id);
            });

            for (auto& cid : expired) {
                sub_map_.erase_client(cid);
                shared_store_.remove_client(cid);
                sessions_.remove(cid);
                invalidate_route_cache();
            }
        }

        for (auto& cid : expired) {
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
            conn->write_ch.close();
            conn->write_wake_ch.close();
            if (conn->session) co_await cleanup_connection(conn);
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

auto broker::writer_loop(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    std::deque<detail::outbound_packet> high_queue;
    std::deque<detail::outbound_packet> normal_queue;
    std::vector<detail::outbound_packet> incoming;
    incoming.reserve(256);

    struct pending_write_result {
        std::shared_ptr<channel<std::expected<std::size_t, std::error_code>>> completion;
        std::size_t logical_size = 0;
    };

    auto complete_one = [](const pending_write_result& pending,
                           std::expected<std::size_t, std::error_code> result) {
        if (pending.completion) {
            (void)pending.completion->try_send(std::move(result));
        }
    };

    auto fail_packet = [](detail::outbound_packet& pkt, std::error_code ec) {
        if (pkt.completion) {
            (void)pkt.completion->try_send(std::unexpected(ec));
        }
    };

    auto fail_pending_queues = [&] (std::error_code ec) {
        for (auto& pkt : high_queue) fail_packet(pkt, ec);
        for (auto& pkt : normal_queue) fail_packet(pkt, ec);
        high_queue.clear();
        normal_queue.clear();

        while (auto pkt = conn->write_ch.try_receive()) {
            fail_packet(*pkt, ec);
        }
    };

    auto ingest = [&](std::size_t max_items) {
        if (max_items == 0) return;
        incoming.clear();
        (void)conn->write_ch.try_receive_many(incoming, max_items);
        for (auto& pkt : incoming) {
            if (pkt.priority == outbound_priority::high) {
                high_queue.push_back(std::move(pkt));
            } else {
                normal_queue.push_back(std::move(pkt));
            }
        }
    };

    auto enqueue_packet = [&](detail::outbound_packet pkt) {
        if (pkt.priority == outbound_priority::high) {
            high_queue.push_back(std::move(pkt));
        } else {
            normal_queue.push_back(std::move(pkt));
        }
    };

    auto write_bytes = [&](const std::string& data)
        -> task<std::expected<std::size_t, std::error_code>> {
        co_return co_await conn->raw_write(data);
    };

    while (true) {
        const auto max_messages = std::max<std::size_t>(1, opts_.write_batch_messages);
        const auto queue_low_water = std::max<std::size_t>(8, max_messages * 2);
        if (high_queue.empty() && normal_queue.size() < queue_low_water) {
            ingest(queue_low_water - normal_queue.size());
        }
        if (high_queue.empty() && normal_queue.empty()) {
            if (conn->write_ch.is_closed()) break;
            auto first = co_await conn->write_ch.receive();
            if (!first) break;
            enqueue_packet(std::move(*first));
            if (high_queue.empty() && normal_queue.size() < queue_low_water) {
                ingest(queue_low_water - normal_queue.size());
            }
        }

        std::string data;
        std::vector<pending_write_result> pending;
        if (!high_queue.empty()) {
            auto pkt = std::move(high_queue.front());
            high_queue.pop_front();
            pending.push_back({
                .completion = std::move(pkt.completion),
                .logical_size = pkt.logical_size,
            });
            data = std::move(pkt.data);
        } else if (!normal_queue.empty()) {
            auto pkt = std::move(normal_queue.front());
            normal_queue.pop_front();
            pending.push_back({
                .completion = std::move(pkt.completion),
                .logical_size = pkt.logical_size,
            });
            data = std::move(pkt.data);

            if (pkt.batchable) {
                const auto max_bytes = std::max<std::size_t>(data.size(), opts_.write_batch_bytes);
                std::size_t messages = 1;
                while (messages < max_messages) {
                    if (normal_queue.empty()) {
                        ingest(max_messages - messages);
                    }
                    if (normal_queue.empty()) break;

                    auto& peek = normal_queue.front();
                    if (!peek.batchable || peek.priority != outbound_priority::normal) break;
                    if (data.size() + peek.data.size() > max_bytes) break;

                    auto next = std::move(normal_queue.front());
                    normal_queue.pop_front();
                    data += next.data;
                    pending.push_back({
                        .completion = std::move(next.completion),
                        .logical_size = next.logical_size,
                    });
                    ++messages;
                }
            }
        } else {
            continue;
        }

        auto wr = co_await write_bytes(data);
        if (!wr) {
            auto ec = wr.error();
            for (auto& p : pending) complete_one(p, std::unexpected(ec));
            fail_pending_queues(ec);
            write_failures_.fetch_add(1, std::memory_order_relaxed);
            conn->connected = false;
            conn->delivery_ch.close();
            conn->write_ch.close();
            conn->write_wake_ch.close();
            conn->sock.close();
            break;
        }

        socket_writes_.fetch_add(1, std::memory_order_relaxed);
        socket_write_bytes_.fetch_add(data.size(), std::memory_order_relaxed);
        std::size_t logical_messages = 0;
        for (auto& p : pending) {
            logical_messages += std::max<std::size_t>(1, p.logical_size);
        }
        if (logical_messages > 1) {
            batch_writes_.fetch_add(1, std::memory_order_relaxed);
            batch_messages_.fetch_add(logical_messages, std::memory_order_relaxed);
        }

        for (auto& p : pending) {
            complete_one(p,
                std::expected<std::size_t, std::error_code>{p.logical_size});
        }
    }

    fail_pending_queues(std::make_error_code(std::errc::broken_pipe));
    conn->delivery_ch.close();
    conn->write_ch.close();
    conn->write_wake_ch.close();
    conn->sock.close();
}

auto broker::handle_connection(socket client, io_context& io, bool use_tls) -> task<void> {
    auto conn = std::make_shared<detail::conn_state>(
        std::move(client), io, opts_.delivery_channel_size, opts_.write_channel_size);

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

    spawn(io, writer_loop(conn));

    auto frame_r = co_await read_frame(*conn);
    if (!frame_r) {
        conn->write_ch.close();
        conn->write_wake_ch.close();
        conn->sock.close();
        co_return;
    }

    if (frame_r->type != control_packet_type::connect) {
        conn->write_ch.close();
        conn->write_wake_ch.close();
        conn->sock.close();
        co_return;
    }

    auto ok = co_await handle_connect(*frame_r, conn);
    if (!ok) {
        conn->write_ch.close();
        conn->write_wake_ch.close();
        co_return;
    }

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
    const auto max_batch_messages = std::max<std::size_t>(1, opts_.write_batch_messages);
    if (max_batch_messages == 1) {
        while (conn->connected) {
            auto received = co_await conn->delivery_ch.receive();
            if (!received) break;
            auto msg_opt = std::optional<publish_message>{std::move(*received)};

            if (!conn->connected) break;

            auto& msg = *msg_opt;

            if (msg.qos_value != qos::at_most_once && conn->session) {
                if (conn->session->inflight_out.size() >=
                    static_cast<std::size_t>(conn->session->receive_maximum)) {
                    conn->session->enqueue_offline(std::move(msg));
                    offline_enqueued_messages_.fetch_add(1, std::memory_order_relaxed);
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
                if (pid != 0 && conn->session) conn->session->release_packet_id(pid);
                logger::debug("mqtt delivery: packet too large for client={}",
                    conn->session ? conn->session->client_id : "?");
                continue;
            }

            if (msg.qos_value == qos::at_most_once) {
                auto queued = co_await conn->submit_write_queued(
                    std::move(pkt),
                    outbound_priority::normal,
                    false);
                if (!queued) {
                    write_failures_.fetch_add(1, std::memory_order_relaxed);
                    conn->connected = false;
                    break;
                }
                delivered_messages_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            if (msg.qos_value != qos::at_most_once && conn->session) {
                inflight_message im;
                im.packet_id    = pid;
                im.msg          = msg;
                im.expected_ack = (msg.qos_value == qos::at_least_once)
                    ? control_packet_type::puback : control_packet_type::pubrec;
                im.send_time    = std::chrono::steady_clock::now();
                conn->session->inflight_out.push_back(std::move(im));
            }

            auto wr = co_await conn->do_write(
                std::move(pkt),
                outbound_priority::normal,
                false);
            if (!wr) {
                if (msg.qos_value != qos::at_most_once && conn->session) {
                    conn->session->release_packet_id(pid);
                    std::erase_if(conn->session->inflight_out, [&](const inflight_message& im) {
                        return im.packet_id == pid;
                    });
                }
                write_failures_.fetch_add(1, std::memory_order_relaxed);
                conn->connected = false;
                break;
            }
            delivered_messages_.fetch_add(1, std::memory_order_relaxed);
        }
        co_return;
    }

    const auto max_batch_bytes = std::max<std::size_t>(1, opts_.write_batch_bytes);
    std::vector<publish_message> batch;
    batch.reserve(max_batch_messages);
    std::string qos0_batch;
    std::size_t qos0_batch_messages = 0;

    auto flush_qos0_batch = [&]() -> task<bool> {
        if (qos0_batch.empty()) {
            co_return true;
        }

        const auto delivered_count = qos0_batch_messages;
        auto queued = co_await conn->submit_write_queued(
            std::move(qos0_batch),
            outbound_priority::normal,
            opts_.write_batch_messages > 1,
            delivered_count);
        qos0_batch.clear();
        qos0_batch_messages = 0;

        if (!queued) {
            write_failures_.fetch_add(1, std::memory_order_relaxed);
            conn->connected = false;
            co_return false;
        }

        delivered_messages_.fetch_add(delivered_count, std::memory_order_relaxed);
        co_return true;
    };

    while (conn->connected) {
        auto received = co_await conn->delivery_ch.receive();
        if (!received) break;

        batch.clear();
        batch.push_back(std::move(*received));
        if (max_batch_messages > 1) {
            (void)conn->delivery_ch.try_receive_many(
                batch,
                max_batch_messages - 1);
        }

        for (auto& msg : batch) {
            if (!conn->connected) break;

            if (msg.qos_value != qos::at_most_once && conn->session) {
                if (!co_await flush_qos0_batch()) {
                    break;
                }
                if (conn->session->inflight_out.size() >=
                    static_cast<std::size_t>(conn->session->receive_maximum)) {
                    conn->session->enqueue_offline(std::move(msg));
                    offline_enqueued_messages_.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
            }

            std::uint16_t pid = 0;
            if (msg.qos_value != qos::at_most_once && conn->session)
                pid = conn->session->alloc_packet_id();

            const auto pkt_size = encoded_publish_size(
                msg.topic, msg.payload, msg.qos_value, conn->version, msg.props);

            if (conn->max_packet_size > 0 && pkt_size > conn->max_packet_size) {
                if (pid != 0 && conn->session) conn->session->release_packet_id(pid);
                logger::debug("mqtt delivery: packet too large for client={}",
                    conn->session ? conn->session->client_id : "?");
                continue;
            }

            if (msg.qos_value == qos::at_most_once) {
                if (!qos0_batch.empty() &&
                    (qos0_batch_messages >= max_batch_messages ||
                     qos0_batch.size() + pkt_size > max_batch_bytes)) {
                    if (!co_await flush_qos0_batch()) {
                        break;
                    }
                }

                append_publish(
                    qos0_batch,
                    msg.topic,
                    msg.payload,
                    msg.qos_value,
                    false,
                    false,
                    0,
                    conn->version,
                    msg.props);
                ++qos0_batch_messages;

                if (qos0_batch_messages >= max_batch_messages ||
                    qos0_batch.size() >= max_batch_bytes) {
                    if (!co_await flush_qos0_batch()) {
                        break;
                    }
                }
                continue;
            }

            auto pkt = encode_publish(
                msg.topic, msg.payload, msg.qos_value,
                false, false, pid, conn->version, msg.props);

            if (msg.qos_value != qos::at_most_once && conn->session) {
                inflight_message im;
                im.packet_id    = pid;
                im.msg          = msg;
                im.expected_ack = (msg.qos_value == qos::at_least_once)
                    ? control_packet_type::puback : control_packet_type::pubrec;
                im.send_time    = std::chrono::steady_clock::now();
                conn->session->inflight_out.push_back(std::move(im));
            }

            auto wr = co_await conn->do_write(
                std::move(pkt),
                outbound_priority::normal,
                false);
            if (!wr) {
                if (msg.qos_value != qos::at_most_once && conn->session) {
                    conn->session->release_packet_id(pid);
                    std::erase_if(conn->session->inflight_out, [&](const inflight_message& im) {
                        return im.packet_id == pid;
                    });
                }
                write_failures_.fetch_add(1, std::memory_order_relaxed);
                conn->connected = false;
                break;
            }
            delivered_messages_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (conn->connected) {
        (void)co_await flush_qos0_batch();
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

            auto wr = co_await conn->do_write(std::move(pkt));
            if (!wr) {
                write_failures_.fetch_add(1, std::memory_order_relaxed);
                conn->connected = false;
                break;
            }

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
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
        auto pkt = encode_connack(false,
            static_cast<std::uint8_t>(connect_return_code::unacceptable_protocol_version),
            protocol_version::v3_1_1);
        (void)co_await conn->do_write(std::move(pkt), outbound_priority::high);
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
            (void)co_await conn->do_write(
                encode_connack(false, rc, cd.version), outbound_priority::high);
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
            (void)co_await conn->do_write(
                encode_connack(false, rc, cd.version), outbound_priority::high);
            co_return false;
        }
    }

    std::shared_ptr<detail::conn_state> takeover_conn;
    bool session_present = false;
    {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);

        auto* existing = sessions_.find(cd.client_id);
        if (existing && existing->online) {
            logger::info("mqtt session takeover client={}", cd.client_id);
            existing->go_offline();
        }

        auto [ss_ref, present] = sessions_.create_or_resume(
            cd.client_id, cd.clean_session, cd.version);
        auto& ss = ss_ref;
        session_present = present;

        ss.keep_alive = conn->keep_alive;
        ss.will_msg   = cd.will_msg;
        ss.username   = cd.username;
        ss.max_offline_queue = opts_.max_offline_queue;

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

        if (cd.clean_session) {
            sub_map_.erase_client(cd.client_id);
            shared_store_.remove_client(cd.client_id);
            invalidate_route_cache();
        } else if (session_present) {
            for (auto& [filter, entry] : ss.subscriptions) {
                auto shared = parse_shared_subscription(filter);
                if (shared && !shared->share_name.empty()) {
                    shared_subscriptions_present_.store(true, std::memory_order_release);
                    sub_map_.insert(shared->topic_filter, cd.client_id, entry);
                    shared_store_.add_member(shared->share_name,
                        shared->topic_filter, cd.client_id);
                } else {
                    sub_map_.insert(filter, cd.client_id, entry);
                }
            }
        }
    }

    {
        co_await channels_rw_.lock();
        async_unique_lock_guard wg(channels_rw_, std::adopt_lock);
        auto it = online_channels_.find(cd.client_id);
        if (it != online_channels_.end()) {
            takeover_conn = it->second.conn.lock();
            online_channels_.erase(it);
        }
        online_channels_[cd.client_id] = online_delivery_target{.conn = conn};
        invalidate_route_cache();
    }
    if (takeover_conn) {
        takeover_conn->delivery_ch.close();
        takeover_conn->write_ch.close();
        takeover_conn->write_wake_ch.close();
        takeover_conn->sock.close();
    }

    if (cd.version == protocol_version::v5 && opts_.max_keep_alive > 0
        && conn->keep_alive > opts_.max_keep_alive) {
        conn->keep_alive = opts_.max_keep_alive;
        if (conn->session) {
            co_await state_mtx_.lock();
            async_lock_guard sg(state_mtx_, std::adopt_lock);
            conn->session->keep_alive = conn->keep_alive;
        }
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
        encode_connack(session_present, rc, cd.version, connack_props),
        outbound_priority::high);
    if (!wr) {
        write_failures_.fetch_add(1, std::memory_order_relaxed);
        co_return false;
    }
    accepted_connections_.fetch_add(1, std::memory_order_relaxed);
    active_connections_.fetch_add(1, std::memory_order_relaxed);

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
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
        logger::warn("mqtt decode PUBLISH failed: {}", msg_r.error());
        co_await send_disconnect_and_close(conn,
            static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
        co_return;
    }
    auto& msg = *msg_r;

    if (static_cast<std::uint8_t>(msg.qos_value) >
        static_cast<std::uint8_t>(opts_.maximum_qos)) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
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
                protocol_errors_.fetch_add(1, std::memory_order_relaxed);
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
        (void)co_await conn->do_write(
            encode_puback(msg.packet_id, conn->version), outbound_priority::high);
    }

    if (msg.qos_value == qos::exactly_once && msg.packet_id != 0) {
        if (conn->session) {
            conn->session->qos2_received.insert(msg.packet_id);
            conn->session->qos2_pending_publish[msg.packet_id] = msg;
        }
        (void)co_await conn->do_write(
            encode_pubrec(msg.packet_id, conn->version), outbound_priority::high);
        co_return;
    }

    if (msg.retain) {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        auto topic = msg.topic;
        retained_.store(topic, retained_message{
            topic, msg.payload.str(), msg.qos_value, msg.props});
    }

    co_await route_publish(conn->io, msg, conn->session ? conn->session->client_id : "");
}
auto broker::handle_puback(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
    -> task<void>
{
    auto ack_r = decode_ack(frame.payload, conn->version);
    if (!ack_r) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
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
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
        logger::warn("mqtt decode PUBREC failed: {}", ack_r.error());
        co_await send_disconnect_and_close(conn,
            static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
        co_return;
    }
    (void)co_await conn->do_write(
        encode_pubrel(ack_r->packet_id, conn->version), outbound_priority::high);
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
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
        logger::warn("mqtt decode PUBREL failed: {}", ack_r.error());
        co_await send_disconnect_and_close(conn,
            static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
        co_return;
    }
    (void)co_await conn->do_write(
        encode_pubcomp(ack_r->packet_id, conn->version), outbound_priority::high);
    if (conn->session) {
        conn->session->qos2_received.erase(ack_r->packet_id);
        auto it = conn->session->qos2_pending_publish.find(ack_r->packet_id);
        if (it != conn->session->qos2_pending_publish.end()) {
            auto msg = std::move(it->second);
            conn->session->qos2_pending_publish.erase(it);
            if (msg.retain) {
                co_await state_mtx_.lock();
                async_lock_guard sg(state_mtx_, std::adopt_lock);
                auto topic = msg.topic;
                retained_.store(topic, retained_message{
                    topic, msg.payload.str(), msg.qos_value, msg.props});
            }
            co_await route_publish(conn->io, msg, conn->session->client_id);
        }
    }
}

auto broker::handle_pubcomp(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
    -> task<void>
{
    auto ack_r = decode_ack(frame.payload, conn->version);
    if (!ack_r) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
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
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
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

        bool is_new = false;
        {
            co_await state_mtx_.lock();
            async_lock_guard sg(state_mtx_, std::adopt_lock);
            is_new = conn->session->add_subscription(entry);

            auto shared = parse_shared_subscription(entry.topic_filter);
            if (shared && !shared->share_name.empty()) {
                shared_subscriptions_present_.store(true, std::memory_order_release);
                sub_map_.insert(shared->topic_filter, conn->session->client_id, entry);
                shared_store_.add_member(shared->share_name, shared->topic_filter,
                    conn->session->client_id);
            } else {
                sub_map_.insert(entry.topic_filter, conn->session->client_id, entry);
            }
            invalidate_route_cache();
        }

        return_codes.push_back(static_cast<std::uint8_t>(entry.max_qos));

        if (is_new || entry.rh == retain_handling::send) {
            auto actual = extract_topic_filter(entry.topic_filter);
            co_await deliver_retained(conn, entry, actual);
        }
    }

    (void)co_await conn->do_write(
        encode_suback(sub.packet_id, return_codes, conn->version, sub.props),
        outbound_priority::high);
}
auto broker::handle_unsubscribe(const mqtt_frame& frame, std::shared_ptr<detail::conn_state> conn)
    -> task<void>
{
    auto unsub_r = decode_unsubscribe(frame.payload, conn->version);
    if (!unsub_r) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
        logger::warn("mqtt decode UNSUBSCRIBE failed: {}", unsub_r.error());
        co_await send_disconnect_and_close(conn,
            static_cast<std::uint8_t>(v5::disconnect_reason_code::malformed_packet));
        co_return;
    }
    if (!conn->session) co_return;

    auto& unsub = *unsub_r;
    std::vector<std::uint8_t> reason_codes;

    for (auto& tf : unsub.topic_filters) {
        bool existed = false;
        {
            co_await state_mtx_.lock();
            async_lock_guard sg(state_mtx_, std::adopt_lock);
            existed = conn->session->remove_subscription(tf);
            auto shared = parse_shared_subscription(tf);
            if (shared && !shared->share_name.empty()) {
                sub_map_.erase(shared->topic_filter, conn->session->client_id);
                shared_store_.remove_member(shared->share_name,
                    shared->topic_filter, conn->session->client_id);
            } else {
                sub_map_.erase(tf, conn->session->client_id);
            }
            invalidate_route_cache();
        }
        if (conn->version == protocol_version::v5)
            reason_codes.push_back(existed
                ? static_cast<std::uint8_t>(v5::unsuback_reason_code::success)
                : static_cast<std::uint8_t>(v5::unsuback_reason_code::no_subscription_existed));
    }

    (void)co_await conn->do_write(
        encode_unsuback(unsub.packet_id, conn->version, reason_codes),
        outbound_priority::high);
}

auto broker::handle_pingreq(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    (void)co_await conn->do_write(encode_pingresp(), outbound_priority::high);
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
    conn->write_ch.close();
    conn->write_wake_ch.close();
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
                encode_auth(resp->first, resp->second), outbound_priority::high);
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
    conn->write_ch.close();
    conn->write_wake_ch.close();
    conn->sock.close();
}

auto broker::will_delay_task(io_context& io, std::string client_id,
                     will will_msg, std::uint32_t delay_sec) -> task<void>
{
    co_await async_sleep(io, std::chrono::seconds(delay_sec));
    {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        auto* ss = sessions_.find(client_id);
        if (ss && ss->online) co_return;
    }

    publish_message wp;
    wp.topic = will_msg.topic;   wp.payload = will_msg.message;
    wp.qos_value = will_msg.qos_value; wp.retain = will_msg.retain;
    wp.props = will_msg.props;
    if (wp.retain) {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        auto topic = wp.topic;
        retained_.store(topic, retained_message{
            topic, wp.payload.str(), wp.qos_value, wp.props});
    }
    co_await route_publish(io, wp, client_id);
    logger::info("mqtt will delayed published client={}", client_id);
}

auto broker::publish_will(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    if (!conn->session || !conn->session->will_msg) co_return;
    auto& w = *conn->session->will_msg;
    publish_message wp;
    wp.topic = w.topic;   wp.payload = w.message;
    wp.qos_value = w.qos_value; wp.retain = w.retain;
    wp.props = w.props;
    if (wp.retain) {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        auto topic = wp.topic;
        retained_.store(topic, retained_message{
            topic, wp.payload.str(), wp.qos_value, wp.props});
    }
    co_await route_publish(conn->io, wp, conn->session->client_id);
    conn->session->will_msg.reset();
}

auto broker::cleanup_connection(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    if (!conn->session) co_return;
    if (conn->cleanup_done) co_return;
    conn->cleanup_done = true;
    active_connections_.fetch_sub(1, std::memory_order_relaxed);
    auto cid = conn->session->client_id;
    {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        conn->session->go_offline();
    }
    {
        co_await channels_rw_.lock();
        async_unique_lock_guard wg(channels_rw_, std::adopt_lock);
        online_channels_.erase(cid);
        invalidate_route_cache();
    }
    {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        if (conn->session->clean_session && conn->session->session_expiry_interval == 0) {
            sub_map_.erase_client(cid);
            shared_store_.remove_client(cid);
            sessions_.remove(cid);
            invalidate_route_cache();
        }
    }
}
auto broker::route_publish(io_context& origin_io,
                           const publish_message& msg,
                           const std::string& sender_cid)
    -> task<void>
{
    routed_messages_.fetch_add(1, std::memory_order_relaxed);
    std::unordered_map<std::string, subscribe_entry> targets;
    const bool shared_present =
        shared_subscriptions_present_.load(std::memory_order_acquire);
    if (!shared_present) {
        auto cache = std::atomic_load_explicit(&route_cache_, std::memory_order_acquire);
        if (cache) {
            auto it = cache->find(msg.topic);
            if (it != cache->end()) {
                targets.reserve(it->second.size());
                for (auto& [cid, entry] : it->second) {
                    if (cid != sender_cid || !entry.no_local) {
                        targets.try_emplace(cid, entry);
                    }
                }
            }
        }
    }

    if (targets.empty()) {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        auto matches = sub_map_.match(msg.topic);

        targets.reserve(matches.size());
        for (auto& m : matches) {
            if (m.client_id == sender_cid && m.entry.no_local) continue;
            targets.try_emplace(m.client_id, m.entry);
        }

        if (shared_present) {
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
                if (!targets.contains(cid)) {
                    subscribe_entry se;
                    se.topic_filter = filter;
                    se.max_qos = msg.qos_value;
                    targets.emplace(cid, std::move(se));
                }
            }
        } else {
            route_cache_value cached;
            cached.reserve(targets.size());
            for (auto& [cid, entry] : targets) {
                cached.emplace_back(cid, entry);
            }
            auto old_cache = std::atomic_load_explicit(&route_cache_, std::memory_order_acquire);
            auto next_cache = std::make_shared<route_cache_map>(
                old_cache ? *old_cache : route_cache_map{});
            next_cache->insert_or_assign(msg.topic, std::move(cached));
            std::atomic_store_explicit(
                &route_cache_,
                std::shared_ptr<const route_cache_map>{std::move(next_cache)},
                std::memory_order_release);
        }
    }

    std::unordered_map<std::string, std::shared_ptr<detail::conn_state>> online_targets;
    online_targets.reserve(targets.size());
    if (!targets.empty()) {
        co_await channels_rw_.lock_shared();
        async_shared_lock_guard rg(channels_rw_, std::adopt_lock);
        for (auto& [cid, _] : targets) {
            auto it = online_channels_.find(cid);
            if (it != online_channels_.end()) {
                if (auto conn = it->second.conn.lock(); conn && conn->connected) {
                    online_targets.emplace(cid, std::move(conn));
                }
            }
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

        auto ch_it = online_targets.find(cid);
        if (ch_it != online_targets.end()) {
            if (co_await deliver_to_connection(origin_io, ch_it->second, std::move(fwd))) {
                continue;
            }
        }

        {
            co_await state_mtx_.lock();
            async_lock_guard sg(state_mtx_, std::adopt_lock);
            auto* ss = sessions_.find(cid);
            if (ss) {
                ss->enqueue_offline(std::move(fwd));
                offline_enqueued_messages_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

auto broker::deliver_retained(std::shared_ptr<detail::conn_state> conn,
                      const subscribe_entry& entry,
                      const std::string& actual_filter) -> task<void>
{
    std::vector<retained_message> matches;
    {
        co_await state_mtx_.lock();
        async_lock_guard sg(state_mtx_, std::adopt_lock);
        matches = retained_.match(actual_filter);
    }
    for (auto& rm : matches) {
        auto eq = static_cast<qos>(std::min(
            static_cast<std::uint8_t>(rm.qos_value),
            static_cast<std::uint8_t>(entry.max_qos)));
        std::uint16_t pid = 0;
        if (eq != qos::at_most_once && conn->session)
            pid = conn->session->alloc_packet_id();

        if (eq != qos::at_most_once && conn->session) {
            inflight_message im;
            im.packet_id = pid;
            im.msg.topic = rm.topic;
            im.msg.payload = rm.payload;
            im.msg.qos_value = eq;
            im.msg.retain = true;
            im.msg.props = rm.props;
            im.expected_ack = (eq == qos::at_least_once)
                ? control_packet_type::puback : control_packet_type::pubrec;
            im.send_time = std::chrono::steady_clock::now();
            conn->session->inflight_out.push_back(std::move(im));
        }

        auto wr = co_await conn->do_write(encode_publish(
            rm.topic, rm.payload, eq,
            true, false, pid,
            conn->version, rm.props),
            outbound_priority::normal,
            eq == qos::at_most_once &&
                opts_.write_batch_messages > 1);
        if (!wr) {
            write_failures_.fetch_add(1, std::memory_order_relaxed);
            if (eq != qos::at_most_once && conn->session) {
                conn->session->release_packet_id(pid);
                std::erase_if(conn->session->inflight_out, [&](const inflight_message& im) {
                    return im.packet_id == pid;
                });
            }
        } else {
            delivered_messages_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

auto broker::deliver_inflight_resend(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    if (!conn->session) co_return;
    for (auto& im : conn->session->inflight_out) {
        if (im.expected_ack == control_packet_type::puback ||
            im.expected_ack == control_packet_type::pubrec) {
            auto wr = co_await conn->do_write(encode_publish(
                im.msg.topic, im.msg.payload, im.msg.qos_value,
                false, true, im.packet_id, conn->version, im.msg.props),
                outbound_priority::normal,
                im.msg.qos_value == qos::at_most_once &&
                    opts_.write_batch_messages > 1);
            if (!wr) {
                write_failures_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            im.send_time = std::chrono::steady_clock::now();
        } else if (im.expected_ack == control_packet_type::pubcomp) {
            auto wr = co_await conn->do_write(
                encode_pubrel(im.packet_id, conn->version), outbound_priority::high);
            if (!wr) {
                write_failures_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
    }
}

auto broker::deliver_offline_queue(std::shared_ptr<detail::conn_state> conn) -> task<void> {
    if (!conn->session) co_return;
    auto& queue = conn->session->offline_queue;
    std::vector<publish_message> remaining;
    remaining.reserve(queue.size());
    bool keep_rest = false;
    for (auto& msg : queue) {
        if (keep_rest) {
            remaining.push_back(std::move(msg));
            continue;
        }
        if (session_state::check_message_expiry(msg)) continue;

        std::uint16_t pid = 0;
        if (msg.qos_value != qos::at_most_once)
            pid = conn->session->alloc_packet_id();
        if (msg.qos_value != qos::at_most_once) {
            inflight_message im;
            im.packet_id = pid;  im.msg = msg;
            im.expected_ack = (msg.qos_value == qos::at_least_once)
                ? control_packet_type::puback : control_packet_type::pubrec;
            im.send_time = std::chrono::steady_clock::now();
            conn->session->inflight_out.push_back(std::move(im));
        }
        auto wr = co_await conn->do_write(encode_publish(
            msg.topic, msg.payload, msg.qos_value,
            false, false, pid, conn->version, msg.props),
            outbound_priority::normal,
            msg.qos_value == qos::at_most_once &&
                opts_.write_batch_messages > 1);
        if (!wr) {
            write_failures_.fetch_add(1, std::memory_order_relaxed);
            if (msg.qos_value != qos::at_most_once) {
                conn->session->release_packet_id(pid);
                std::erase_if(conn->session->inflight_out, [&](const inflight_message& im) {
                    return im.packet_id == pid;
                });
            }
            remaining.push_back(std::move(msg));
            keep_rest = true;
            continue;
        } else {
            delivered_messages_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    queue = std::move(remaining);
}

auto broker::send_disconnect_and_close(
    std::shared_ptr<detail::conn_state> conn,
    std::uint8_t reason_code
) -> task<void>
{
    if (conn->version == protocol_version::v5) {
        auto pkt = encode_disconnect(conn->version, reason_code);
        (void)co_await conn->do_write(std::move(pkt), outbound_priority::high);
    }
    conn->connected = false;
    conn->delivery_ch.close();
    conn->write_ch.close();
    conn->write_wake_ch.close();
}

auto broker::generate_client_id() -> std::string {
    static std::atomic<std::uint64_t> counter{0};
    return "auto-" + std::to_string(
        counter.fetch_add(1, std::memory_order_relaxed));
}

} // namespace cnetmod::mqtt
