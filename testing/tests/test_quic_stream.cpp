#include "test_framework.hpp"
#include <cnetmod/config.hpp>
#ifdef CNETMOD_PLATFORM_LINUX
    #include <arpa/inet.h>
    #include <errno.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

import std;
import cnetmod.coro.task;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.protocol.udp;
import cnetmod.protocol.quic;

TEST(quic_stream_fin_cannot_shrink_received_extent)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(1024);
    const std::array data{std::byte{'x'}};
    ASSERT_TRUE(stream.push_received(100, data, false).has_value());
    ASSERT_FALSE(stream.push_received(10, {}, true).has_value());
}

TEST(quic_stream_reset_cannot_exceed_receive_credit)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(16);
    ASSERT_FALSE(stream.reset_remote(1024).has_value());
}

TEST(quic_stream_reset_final_size_is_accounted)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(1024);
    ASSERT_TRUE(stream.reset_remote(100).has_value());
    ASSERT_EQ(stream.bytes_received(), 100U);
}

TEST(quic_stream_out_of_order_gap_is_not_readable)
{
    cnetmod::quic::quic_stream stream{
        0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(1024);
    stream.init();

    const std::array later{
        std::byte{'w'}, std::byte{'o'}, std::byte{'r'}, std::byte{'l'},
        std::byte{'d'}};
    const auto buffered = stream.push_received(5, later, true);
    ASSERT_TRUE(buffered.has_value());
    ASSERT_FALSE(stream.is_readable());

    const std::array first{
        std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'},
        std::byte{'o'}};
    const auto completed_gap = stream.push_received(0, first, false);
    ASSERT_TRUE(completed_gap.has_value());
    ASSERT_TRUE(stream.is_readable());
}

TEST(quic_stream_duplicate_fin_is_idempotent)
{
    cnetmod::quic::quic_stream stream{
        0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(1024);
    stream.init();

    const auto first_fin = stream.push_received(0, {}, true);
    ASSERT_TRUE(first_fin.has_value());
    ASSERT_TRUE(stream.is_readable());

    const auto duplicate_fin = stream.push_received(0, {}, true);
    ASSERT_TRUE(duplicate_fin.has_value());

    const std::array invalid{std::byte{'x'}};
    const auto beyond_final_size = stream.push_received(0, invalid, true);
    ASSERT_FALSE(beyond_final_size.has_value());
    ASSERT_EQ(beyond_final_size.error(),
        cnetmod::quic::make_error_code(
            cnetmod::quic::quic_errc::final_size_error));
}

TEST(quic_stream_initial_peer_send_credit_can_be_zero)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_send_limit(0);
    stream.init();
    const std::array data{std::byte{'x'}};
    const auto rejected = cnetmod::sync_wait(stream.send(data));
    ASSERT_FALSE(rejected.has_value());
    ASSERT_EQ(stream.bytes_sent(), 0U);
    stream.update_send_limit(1);
    const auto sent = cnetmod::sync_wait(stream.send(data));
    ASSERT_TRUE(sent.has_value());
    ASSERT_EQ(stream.bytes_sent(), 1U);
}

TEST(quic_stream_initial_peer_send_credit_can_exceed_fallback)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    constexpr std::size_t size = 300U * 1024U;
    stream.set_initial_send_limit(size);
    stream.init();
    const std::vector<std::byte> data(size, std::byte{'x'});
    const auto sent = cnetmod::sync_wait(stream.send(data));
    ASSERT_TRUE(sent.has_value());
    ASSERT_EQ(stream.bytes_sent(), size);
    const std::array extra{std::byte{'y'}};
    const auto rejected = cnetmod::sync_wait(stream.send(extra));
    ASSERT_FALSE(rejected.has_value());
    ASSERT_EQ(stream.bytes_sent(), size);
}

TEST(quic_stream_public_send_and_close_remain_lazy)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::client, true};
    stream.set_initial_send_limit(2U);
    stream.init();
    const std::array payload{std::byte{'x'}};
    {
        auto dropped = stream.send(payload);
        (void)dropped;
        ASSERT_EQ(stream.bytes_sent(), 0U);
        ASSERT_EQ(stream.remaining_send_window(), 2U);
    }
    ASSERT_EQ(stream.bytes_sent(), 0U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::open);

    auto pending = stream.send(payload);
    ASSERT_EQ(stream.bytes_sent(), 0U);
    const auto sent = cnetmod::sync_wait(std::move(pending));
    ASSERT_TRUE(sent.has_value());
    ASSERT_EQ(stream.bytes_sent(), 1U);
    ASSERT_EQ(stream.remaining_send_window(), 1U);
    {
        auto dropped = stream.close_local();
        (void)dropped;
        ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::open);
    }
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::open);
    auto closing = stream.close_local();
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::open);
    cnetmod::sync_wait(std::move(closing));
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::half_closed_local);
    const auto rejected = cnetmod::sync_wait(stream.send(payload));
    ASSERT_FALSE(rejected.has_value());
    if (!rejected)
        ASSERT_EQ(rejected.error(), std::make_error_code(std::errc::operation_not_permitted));
    ASSERT_EQ(stream.bytes_sent(), 1U);
}

TEST(quic_stream_empty_send_preserves_local_close_behavior)
{
    cnetmod::quic::quic_stream open{0, cnetmod::quic::quic_role::client, true};
    open.init();
    const auto sent = cnetmod::sync_wait(open.send({}));
    ASSERT_TRUE(sent.has_value());
    ASSERT_EQ(open.bytes_sent(), 0U);
    ASSERT_TRUE(open.state() == cnetmod::quic::stream_state::half_closed_local);
    const auto repeated = cnetmod::sync_wait(open.send({}));
    ASSERT_FALSE(repeated.has_value());
    if (!repeated)
        ASSERT_EQ(repeated.error(), std::make_error_code(std::errc::operation_not_permitted));
    ASSERT_EQ(open.bytes_sent(), 0U);

    cnetmod::quic::quic_stream remote_closed{4, cnetmod::quic::quic_role::client, true};
    remote_closed.init();
    remote_closed.close_remote();
    const auto closed = cnetmod::sync_wait(remote_closed.send({}));
    ASSERT_TRUE(closed.has_value());
    ASSERT_TRUE(remote_closed.state() == cnetmod::quic::stream_state::closed);
    ASSERT_EQ(remote_closed.bytes_sent(), 0U);
    cnetmod::sync_wait(remote_closed.close_local());
    ASSERT_TRUE(remote_closed.state() == cnetmod::quic::stream_state::closed);
}

TEST(quic_retired_stream_history_coalesces_without_reopening_old_ids)
{
    cnetmod::quic::retired_stream_history history;
    for (std::uint64_t index{}; index < 100000U; ++index)
        for (std::uint64_t type{}; type < 4U; ++type)
            history.insert(index * 4U + type, {index, index * 2U});
    ASSERT_EQ(history.range_count(), 4U);
    ASSERT_EQ(history.final_size_count(), history.final_size_capacity);
    ASSERT_TRUE(history.contains(0U));
    ASSERT_TRUE(history.contains(399999U));
    ASSERT_FALSE(history.contains(400000U));
    ASSERT_FALSE(history.find_final_sizes(0U).has_value());
    const auto latest = history.find_final_sizes(399999U);
    ASSERT_TRUE(latest.has_value());
    ASSERT_EQ(latest->received_final_size, 99999U);
    ASSERT_EQ(latest->sent_final_size, 199998U);
}

TEST(quic_retired_stream_history_preserves_gaps_and_merges_out_of_order)
{
    cnetmod::quic::retired_stream_history history;
    history.insert(12U, {3U, 30U});
    history.insert(0U, {0U, 0U});
    history.insert(8U, {2U, 20U});
    ASSERT_EQ(history.range_count(), 2U);
    ASSERT_FALSE(history.contains(4U));
    ASSERT_FALSE(history.contains(1U));
    history.insert(4U, {1U, 10U});
    ASSERT_EQ(history.range_count(), 1U);
    ASSERT_TRUE(history.contains(4U));
    history.insert(4U, {99U, 99U});
    ASSERT_EQ(history.find_final_sizes(4U)->received_final_size, 1U);
    ASSERT_EQ(history.final_size_count(), 4U);
}

TEST(quic_stream_reset_duplicates_preserve_credit_and_final_size)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(100);
    ASSERT_TRUE(stream.reset_remote(100).has_value());
    ASSERT_TRUE(stream.reset_remote(100).has_value());
    ASSERT_EQ(stream.bytes_received(), 100U);
    ASSERT_EQ(stream.bytes_consumed(), 100U);
    ASSERT_EQ(stream.remaining_receive_window(), 0U);
    ASSERT_FALSE(stream.reset_remote(99).has_value());
}

TEST(quic_stream_abandoned_receive_accepts_final_size_without_buffering)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(100);
    const std::array data{std::byte{'x'}};
    ASSERT_TRUE(stream.push_received(10, data, false).has_value());
    stream.stop_local();
    stream.abandon_receive();
    ASSERT_TRUE(stream.push_received(50, data, true).has_value());
    ASSERT_EQ(stream.bytes_received(), 51U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::closed);
    ASSERT_TRUE(stream.reset_remote(51).has_value());
    ASSERT_EQ(stream.bytes_consumed(), 51U);
    ASSERT_TRUE(stream.reset_remote(51).has_value());
    ASSERT_FALSE(stream.push_received(52, data, false).has_value());
}

TEST(quic_stream_unidirectional_has_only_one_open_direction)
{
    cnetmod::quic::quic_stream incoming{2, cnetmod::quic::quic_role::server, false};
    incoming.init();
    ASSERT_TRUE(incoming.state() == cnetmod::quic::stream_state::half_closed_local);
    ASSERT_TRUE(incoming.push_received(0, {}, true).has_value());
    ASSERT_TRUE(incoming.state() == cnetmod::quic::stream_state::closed);
    cnetmod::quic::quic_stream outgoing{3, cnetmod::quic::quic_role::server, false};
    outgoing.init();
    ASSERT_TRUE(outgoing.state() == cnetmod::quic::stream_state::half_closed_remote);
    cnetmod::sync_wait(outgoing.close_local());
    ASSERT_TRUE(outgoing.state() == cnetmod::quic::stream_state::closed);
}

TEST(quic_stream_abandon_after_fin_closes_unconsumed_receive_direction)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.init();
    const std::array payload{std::byte{'x'}, std::byte{'y'}};
    ASSERT_TRUE(stream.push_received(0, payload, true).has_value());
    ASSERT_EQ(stream.bytes_consumed(), 0U);
    stream.stop_local();
    stream.abandon_receive();
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::closed);
    ASSERT_TRUE(stream.reset_remote(2).has_value());
    ASSERT_EQ(stream.bytes_consumed(), 2U);
    ASSERT_TRUE(stream.push_received(0, payload, true).has_value());
    ASSERT_TRUE(stream.reset_remote(2).has_value());
    ASSERT_EQ(stream.bytes_consumed(), 2U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::closed);
    stream.abandon_receive();
    cnetmod::sync_wait(stream.close_local());
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::closed);
}

#ifdef CNETMOD_PLATFORM_LINUX
TEST(quic_connection_stop_joins_a_backpressured_kernel_send)
{
    cnetmod::net_init network;
    // UDP drops when its receiver is full. A connected TCP handle supplies
    // real, stable kernel backpressure without a production-only test hook.
    const auto listener_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_TRUE(listener_fd >= 0);
    if (listener_fd < 0) return;
    auto listener = cnetmod::socket::from_native(listener_fd);
    ::sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    const int small_buffer = 4096;
    ASSERT_EQ(::setsockopt(listener_fd, SOL_SOCKET, SO_RCVBUF,
        &small_buffer, sizeof(small_buffer)), 0);
    const auto bound = ::bind(listener_fd, reinterpret_cast<const ::sockaddr*>(&address), sizeof(address));
    ASSERT_EQ(bound, 0);
    if (bound != 0) return;
    const auto listening = ::listen(listener_fd, 1);
    ASSERT_EQ(listening, 0);
    if (listening != 0) return;
    ::socklen_t address_size = sizeof(address);
    ASSERT_EQ(::getsockname(listener_fd, reinterpret_cast<::sockaddr*>(&address), &address_size), 0);
    const auto sender_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_TRUE(sender_fd >= 0);
    if (sender_fd < 0) return;
    auto sender = cnetmod::socket::from_native(sender_fd);
    const auto connected = ::connect(sender_fd, reinterpret_cast<const ::sockaddr*>(&address), sizeof(address));
    ASSERT_EQ(connected, 0);
    if (connected != 0) return;
    const auto receiver_fd = ::accept4(listener_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    ASSERT_TRUE(receiver_fd >= 0);
    if (receiver_fd < 0) return;
    auto receiver = cnetmod::socket::from_native(receiver_fd);
    ASSERT_TRUE(sender.apply_options(cnetmod::socket_options{.send_buffer_size = 4096}).has_value());
    const auto peer = sender.remote_endpoint();
    ASSERT_TRUE(peer.has_value());
    if (!peer) return;
    std::array<std::byte, 4096> filler{};
    std::size_t filler_bytes{};
    for (unsigned settle{}; settle < 2U; ++settle)
    {
        bool blocked{};
        for (unsigned attempt{}; attempt < 4096U; ++attempt)
        {
            const auto sent = ::send(sender_fd, filler.data(), filler.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent < 0)
            {
                blocked = errno == EAGAIN || errno == EWOULDBLOCK;
                break;
            }
            filler_bytes += static_cast<std::size_t>(sent);
        }
        ASSERT_TRUE(blocked);
        if (!blocked) return;
        // The synchronous test driver lets TCP settle its advertised window;
        // no coroutine or executor thread is blocked here.
        if (settle == 0U)
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    auto context = cnetmod::make_io_context();
    auto tls = cnetmod::ssl_context::quic_client();
    ASSERT_TRUE(tls.has_value());
    if (!tls) return;
    tls->configure_alpn_client({"h3"});
    cnetmod::udp::udp_socket socket{*context};
    socket.native_socket() = std::move(sender);
    cnetmod::quic::quic_connection connection{*context, std::move(socket), *peer,
        cnetmod::quic::quic_role::client, *tls};
    std::optional<std::expected<void, std::error_code>> ran;
    bool stopped{};
    auto run = [&]() -> cnetmod::task<void>
    {
        ran = co_await connection.run();
        if (stopped)
            context->stop();
    };
    auto running = run();
    running.handle().resume();
    (void)context->poll();
    ASSERT_FALSE(ran.has_value());
    auto stop = [&]() -> cnetmod::task<void>
    {
        co_await connection.async_stop_io();
        co_await connection.async_close({}, "backpressure regression");
        stopped = true;
        if (ran)
            context->stop();
    };
    auto stopping = stop();
    stopping.handle().resume();
    context->run();
    ASSERT_TRUE(stopped);
    ASSERT_TRUE(ran.has_value());
    ASSERT_TRUE(connection.is_closed());
    // The peer was never drained while QUIC or stop was running. Verify that
    // no ClientHello bytes were accepted: an incomplete run alone might have
    // meant waiting for a response rather than an actually blocked send.
    std::size_t received_bytes{};
    bool eof{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto received = ::recv(receiver_fd, filler.data(), filler.size(), MSG_DONTWAIT);
        if (received == 0)
        {
            eof = true;
            break;
        }
        if (received > 0)
        {
            received_bytes += static_cast<std::size_t>(received);
            ASSERT_TRUE(std::ranges::all_of(std::span{filler.data(), static_cast<std::size_t>(received)},
                [](std::byte value) { return value == std::byte{}; }));
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(eof);
    ASSERT_EQ(received_bytes, filler_bytes);
}
#endif

TEST(posted_resource_cleanup_separates_dispatch_from_discard)
{
    cnetmod::net_init network;
    for (const bool dispatch : {false, true})
    {
        std::size_t called{}, cleaned{}, destroyed{};
        auto sentinel = std::make_shared<int>(0);
        std::weak_ptr<int> weak = sentinel;
        struct payload
        {
            std::shared_ptr<int> sentinel;
            std::size_t* called;
            std::size_t* cleaned;
            std::size_t* destroyed;
            ~payload() { ++*destroyed; }
        };
        auto context = cnetmod::make_io_context();
        context->post([](void* argument)
            {
                std::unique_ptr<payload> owned{static_cast<payload*>(argument)};
                ++*owned->called;
            }, new payload{std::move(sentinel), &called, &cleaned, &destroyed},
            [](void* argument)
            {
                auto* value = static_cast<payload*>(argument);
                ++*value->cleaned;
                delete value;
            });
        ASSERT_FALSE(weak.expired());
        if (dispatch)
        {
            for (unsigned tick{}; tick < 4U && called == 0U; ++tick)
                (void)context->poll();
            ASSERT_EQ(called, 1U);
            ASSERT_EQ(cleaned, 0U);
            ASSERT_EQ(destroyed, 1U);
            ASSERT_TRUE(weak.expired());
        }
        // This tests the public post API's discard contract. The real
        // retire_stream dispatch path has its separate network fixture.
        context.reset();
        ASSERT_EQ(called, dispatch ? 1U : 0U);
        ASSERT_EQ(cleaned, dispatch ? 0U : 1U);
        ASSERT_EQ(destroyed, 1U);
        ASSERT_TRUE(weak.expired());
    }
}

RUN_TESTS();

TEST(quic_stream_receive_remains_lazy_and_consumes_partial_fin)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.init();
    const std::array payload{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    ASSERT_TRUE(stream.push_received(0, payload, true).has_value());
    std::array<std::byte, 2> output{};
    {
        auto dropped = stream.receive(cnetmod::buffer(output));
        (void)dropped;
        ASSERT_EQ(stream.bytes_consumed(), 0U);
    }
    ASSERT_EQ(stream.bytes_consumed(), 0U);
    auto pending = stream.receive(cnetmod::buffer(output));
    ASSERT_EQ(stream.bytes_consumed(), 0U);
    const auto first = cnetmod::sync_wait(std::move(pending));
    ASSERT_TRUE(first.has_value());
    if (first) ASSERT_EQ(*first, 2U);
    ASSERT_TRUE(output[0] == std::byte{'a'} && output[1] == std::byte{'b'});
    ASSERT_EQ(stream.bytes_consumed(), 2U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::open);
    const auto last = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(last.has_value());
    if (last) ASSERT_EQ(*last, 1U);
    ASSERT_TRUE(output[0] == std::byte{'c'});
    ASSERT_EQ(stream.bytes_consumed(), 3U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::half_closed_remote);
    const auto eof = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(eof.has_value());
    if (eof) ASSERT_EQ(*eof, 0U);
}

TEST(quic_stream_pending_receive_observes_later_abandonment)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.init();
    const std::array payload{std::byte{'a'}};
    ASSERT_TRUE(stream.push_received(0, payload, false).has_value());
    std::array<std::byte, 1> output{};
    auto pending = stream.receive(cnetmod::buffer(output));
    stream.abandon_receive();
    const auto received = cnetmod::sync_wait(std::move(pending));
    ASSERT_FALSE(received.has_value());
    if (!received)
        ASSERT_EQ(received.error(), std::make_error_code(std::errc::operation_canceled));
    ASSERT_EQ(stream.bytes_consumed(), 0U);
    ASSERT_TRUE(output[0] == std::byte{});
}

TEST(quic_stream_receive_gap_would_block_until_contiguous_data_arrives)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.init();
    const std::array later{std::byte{'c'}, std::byte{'d'}};
    ASSERT_TRUE(stream.push_received(2, later, true).has_value());
    std::array<std::byte, 4> output{};
    const auto blocked = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_FALSE(blocked.has_value());
    if (!blocked)
        ASSERT_EQ(blocked.error(), std::make_error_code(std::errc::operation_would_block));
    ASSERT_EQ(stream.bytes_consumed(), 0U);
    ASSERT_TRUE(std::ranges::all_of(output, [](std::byte value) { return value == std::byte{}; }));
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::open);

    const std::array first{std::byte{'a'}, std::byte{'b'}};
    ASSERT_TRUE(stream.push_received(0, first, false).has_value());
    const auto received = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(received.has_value());
    if (received) ASSERT_EQ(*received, 4U);
    const std::array expected{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}};
    ASSERT_TRUE(output == expected);
    ASSERT_EQ(stream.bytes_consumed(), 4U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::half_closed_remote);
    const auto eof = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(eof.has_value());
    if (eof) ASSERT_EQ(*eof, 0U);
    ASSERT_EQ(stream.bytes_consumed(), 4U);
}

TEST(quic_stream_receive_multiple_entries_then_partial_fin_and_overlap)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.init();
    const std::array first{std::byte{'a'}, std::byte{'b'}};
    const std::array last{std::byte{'c'}, std::byte{'d'}, std::byte{'e'}};
    ASSERT_TRUE(stream.push_received(2, last, true).has_value());
    ASSERT_TRUE(stream.push_received(0, first, false).has_value());
    std::array<std::byte, 3> output{};
    const auto received = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(received.has_value());
    if (received) ASSERT_EQ(*received, 3U);
    const std::array expected{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    ASSERT_TRUE(output == expected);
    ASSERT_EQ(stream.bytes_consumed(), 3U);
    ASSERT_EQ(stream.bytes_received(), 5U);
    ASSERT_TRUE(stream.is_readable());
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::open);

    const std::array overlap{std::byte{'b'}, std::byte{'c'}, std::byte{'d'}, std::byte{'e'}};
    ASSERT_TRUE(stream.push_received(1, overlap, true).has_value());
    const std::array changed{std::byte{'d'}, std::byte{'x'}};
    ASSERT_FALSE(stream.push_received(3, changed, false).has_value());
    const auto tail = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(tail.has_value());
    if (tail) ASSERT_EQ(*tail, 2U);
    ASSERT_TRUE(output[0] == std::byte{'d'} && output[1] == std::byte{'e'});
    ASSERT_EQ(stream.bytes_consumed(), 5U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::half_closed_remote);
    const auto eof = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(eof.has_value());
    if (eof) ASSERT_EQ(*eof, 0U);
}

TEST(quic_stream_in_order_receive_grows_compacts_and_migrates_on_overlap)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(1U << 20U);
    stream.init();
    // Many in-order frames, past the inline storage, read in odd-sized parts.
    std::vector<std::byte> sent;
    std::vector<std::byte> read;
    std::uint64_t offset{};
    std::array<std::byte, 37> output{};
    for (int round = 0; round < 200; ++round)
    {
        std::vector<std::byte> chunk(static_cast<std::size_t>(23 + round % 97));
        for (auto& byte : chunk)
            byte = static_cast<std::byte>((sent.size() + (&byte - chunk.data())) * 7U);
        ASSERT_TRUE(stream.push_received(offset, chunk, false).has_value());
        offset += chunk.size();
        sent.insert(sent.end(), chunk.begin(), chunk.end());
        if (round % 3 == 0)
        {
            const auto got = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
            ASSERT_TRUE(got.has_value());
            read.insert(read.end(), output.begin(), output.begin() + static_cast<std::ptrdiff_t>(*got));
        }
    }
    // An exact retransmission of unread bytes is accepted; a changed one is not.
    const auto unread = read.size();
    const std::span<const std::byte> again{sent.data() + unread, 10U};
    ASSERT_TRUE(stream.push_received(unread, again, false).has_value());
    std::vector<std::byte> changed(again.begin(), again.end());
    changed[5] ^= std::byte{1};
    ASSERT_FALSE(stream.push_received(unread, changed, false).has_value());
    // A frame beyond a gap moves the in-order bytes into reassembly.
    const std::array tail{std::byte{'y'}, std::byte{'z'}};
    ASSERT_TRUE(stream.push_received(offset + 3U, tail, true).has_value());
    const std::array gap{std::byte{'u'}, std::byte{'v'}, std::byte{'w'}};
    ASSERT_TRUE(stream.push_received(offset, gap, false).has_value());
    sent.insert(sent.end(), gap.begin(), gap.end());
    sent.insert(sent.end(), tail.begin(), tail.end());
    for (;;)
    {
        const auto got = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
        ASSERT_TRUE(got.has_value());
        if (*got == 0U)
            break;
        read.insert(read.end(), output.begin(), output.begin() + static_cast<std::ptrdiff_t>(*got));
    }
    ASSERT_TRUE(read == sent);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::half_closed_remote);
}

TEST(quic_stream_receive_gap_zero_output_then_exact_fin_after_local_close)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.init();
    cnetmod::sync_wait(stream.close_local());
    const std::array first{std::byte{'a'}, std::byte{'b'}};
    const std::array last{std::byte{'e'}, std::byte{'f'}};
    ASSERT_TRUE(stream.push_received(4, last, true).has_value());
    ASSERT_TRUE(stream.push_received(0, first, false).has_value());
    std::array<std::byte, 4> output{};
    const auto received = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(received.has_value());
    if (received) ASSERT_EQ(*received, 2U);
    ASSERT_EQ(stream.bytes_consumed(), 2U);
    ASSERT_FALSE(stream.is_readable());
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::half_closed_local);
    const auto empty = cnetmod::sync_wait(stream.receive(cnetmod::mutable_buffer{nullptr, 0U}));
    ASSERT_FALSE(empty.has_value());
    if (!empty) ASSERT_EQ(empty.error(), std::make_error_code(std::errc::operation_would_block));
    ASSERT_EQ(stream.bytes_consumed(), 2U);
    const std::array hole{std::byte{'c'}, std::byte{'d'}};
    ASSERT_TRUE(stream.push_received(2, hole, false).has_value());
    const auto completed = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(completed.has_value());
    if (completed) ASSERT_EQ(*completed, 4U);
    const std::array expected{std::byte{'c'}, std::byte{'d'}, std::byte{'e'}, std::byte{'f'}};
    ASSERT_TRUE(output == expected);
    ASSERT_EQ(stream.bytes_consumed(), 6U);
    ASSERT_TRUE(stream.state() == cnetmod::quic::stream_state::closed);
    const auto eof = cnetmod::sync_wait(stream.receive(cnetmod::buffer(output)));
    ASSERT_TRUE(eof.has_value());
    if (eof) ASSERT_EQ(*eof, 0U);
}
