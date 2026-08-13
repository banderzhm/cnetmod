#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.coro.awaitable;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.udp;

#ifdef CNETMOD_HAS_EPOLL
    #include <fcntl.h>
    #include <sys/epoll.h>
    #include <sys/socket.h>
    #include <unistd.h>
import cnetmod.io.platform.epoll;
#endif

namespace {

struct batch_probe_result
{
    std::size_t received{};
    bool payloads_match{};
#ifdef CNETMOD_PLATFORM_WINDOWS
    bool rio_active{};
    bool rio_fallback{};
    std::uint64_t rio_receive_completions{};
#endif
};

#ifdef CNETMOD_HAS_EPOLL
struct epoll_readiness_awaiter
{
    cnetmod::epoll_context& context;
    int fd{};
    std::uint32_t events{};
    std::error_code error{};

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        const auto registered = context.add(fd, events | EPOLLONESHOT,
            handle.address());
        if (!registered)
        {
            error = registered.error();
            return false;
        }
        return true;
    }

    auto await_resume() const noexcept -> std::error_code
    {
        return error;
    }
};

auto await_epoll_direction(cnetmod::epoll_context& context, int fd,
    std::uint32_t events, bool& completed) -> cnetmod::task<void>
{
    const auto error = co_await epoll_readiness_awaiter{context, fd, events};
    completed = !error;
}

auto make_nonblocking_socket_pair() -> std::array<int, 2>
{
    std::array<int, 2> pair{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
            pair.data()) != 0)
        return {-1, -1};
    return pair;
}
#endif

#ifdef CNETMOD_PLATFORM_WINDOWS
struct active_socket_backend final : cnetmod::socket_async_state
{
    void on_socket_close() noexcept override {}
};
#endif

auto run_udp_batch_probe(cnetmod::io_context& context)
    -> cnetmod::task<std::expected<batch_probe_result, std::error_code>>
{
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    if (!loopback)
        co_return std::unexpected(loopback.error());

    cnetmod::udp::udp_socket receiver{context};
    cnetmod::socket_options options{};
#ifdef CNETMOD_PLATFORM_WINDOWS
    options.registered_io = true;
#endif
    if (const auto opened = receiver.open(cnetmod::endpoint{*loopback, 0U}, options);
        !opened)
        co_return std::unexpected(opened.error());

    cnetmod::udp::udp_socket sender{context};
    if (const auto opened = sender.open(); !opened)
        co_return std::unexpected(opened.error());
    const auto destination = receiver.native_socket().local_endpoint();
    if (!destination)
        co_return std::unexpected(destination.error());

    const std::array<std::string_view, 4> payloads{"one", "two", "three", "four"};
    const std::array datagrams{
        cnetmod::udp_send_datagram{
            cnetmod::const_buffer{payloads[0].data(), payloads[0].size()}, *destination},
        cnetmod::udp_send_datagram{
            cnetmod::const_buffer{payloads[1].data(), payloads[1].size()}, *destination},
        cnetmod::udp_send_datagram{
            cnetmod::const_buffer{payloads[2].data(), payloads[2].size()}, *destination},
        cnetmod::udp_send_datagram{
            cnetmod::const_buffer{payloads[3].data(), payloads[3].size()}, *destination}};

    const auto submitted = co_await cnetmod::async_sendto_batch(
        context, sender.native_socket(), datagrams);
    if (!submitted)
        co_return std::unexpected(submitted.error());
    if (*submitted != datagrams.size())
        co_return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));

    const auto received = co_await cnetmod::async_recvfrom_batch(
        context, receiver.native_socket(), datagrams.size(), 2048U);
    if (!received)
        co_return std::unexpected(received.error());

    batch_probe_result result{.received = received->size()};
    if (received->size() == payloads.size())
    {
        const auto expected_bytes = [](std::string_view text)
        {
            return std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(text.data()), text.size()};
        };
        result.payloads_match = std::ranges::equal(
                                    (*received)[0].bytes, expected_bytes(payloads[0])) &&
            std::ranges::equal((*received)[1].bytes, expected_bytes(payloads[1])) &&
            std::ranges::equal((*received)[2].bytes, expected_bytes(payloads[2])) &&
            std::ranges::equal((*received)[3].bytes, expected_bytes(payloads[3]));
    }

#ifdef CNETMOD_PLATFORM_WINDOWS
    const auto statistics = receiver.native_socket().async_statistics();
    result.rio_active = statistics.registered_io_active;
    result.rio_fallback = statistics.registered_io_fallback;
    result.rio_receive_completions = statistics.receive_completions;
#endif

    // Close while the RIO ring owns its continuously posted receives. This is
    // the lifecycle edge that previously risked freeing registered memory too
    // early; completion callbacks now retain the ring until it drains.
    receiver.close();
    sender.close();
    co_return result;
}

auto run_udp_batch_cancellation_probe(cnetmod::io_context& context)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    if (!loopback)
        co_return std::unexpected(loopback.error());

    cnetmod::udp::udp_socket receiver{context};
    if (const auto opened = receiver.open(cnetmod::endpoint{*loopback, 0U}); !opened)
        co_return std::unexpected(opened.error());

    cnetmod::cancel_token token;
    const auto received = co_await cnetmod::with_timeout(context,
        std::chrono::milliseconds{25}, cnetmod::async_recvfrom_batch(context, receiver.native_socket(), 4U, 2048U, token), token);
    if (received || received.error() != std::make_error_code(std::errc::timed_out))
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    if (token.reason() != cnetmod::cancellation_reason::deadline_exceeded)
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    co_return {};
}

auto run_udp_batch_short_burst_probe(cnetmod::io_context& context)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    if (!loopback)
        co_return std::unexpected(loopback.error());

    cnetmod::udp::udp_socket receiver{context};
    if (const auto opened = receiver.open(cnetmod::endpoint{*loopback, 0U}); !opened)
        co_return std::unexpected(opened.error());
    cnetmod::udp::udp_socket sender{context};
    if (const auto opened = sender.open(); !opened)
        co_return std::unexpected(opened.error());
    const auto destination = receiver.native_socket().local_endpoint();
    if (!destination)
        co_return std::unexpected(destination.error());

    constexpr std::string_view payload{"short-burst"};
    const auto sent = co_await cnetmod::async_sendto(context, sender.native_socket(),
        cnetmod::const_buffer{payload.data(), payload.size()}, *destination);
    if (!sent || *sent != payload.size())
        co_return std::unexpected(sent ? std::make_error_code(std::errc::io_error)
                                       : sent.error());

    // Asking for a larger batch must return immediately after draining the
    // one datagram that is ready. On Windows this catches a regression where
    // a synchronous WSARecvFrom waited for the remaining seven slots and
    // stalled the entire IOCP listener.
    const auto received = co_await cnetmod::async_recvfrom_batch(
        context, receiver.native_socket(), 8U, 2048U);
    if (!received || received->size() != 1U ||
        !std::ranges::equal((*received)[0].bytes,
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(payload.data()),
                payload.size()}))
        co_return std::unexpected(received ? std::make_error_code(std::errc::protocol_error)
                                           : received.error());
    co_return {};
}

auto run_udp_batch_ordering_stress(cnetmod::io_context& context)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    constexpr std::size_t batch_size = 32U;
    constexpr std::size_t rounds = 24U;
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    if (!loopback)
        co_return std::unexpected(loopback.error());

    // Deliberately use ordinary overlapped UDP even on Windows. This covers
    // the IOCP completion + immediate WSARecvFrom drain path rather than the
    // persistent RIO ring, and verifies datagram order across many batches.
    cnetmod::udp::udp_socket receiver{context};
    if (const auto opened = receiver.open(cnetmod::endpoint{*loopback, 0U}); !opened)
        co_return std::unexpected(opened.error());
    cnetmod::udp::udp_socket sender{context};
    if (const auto opened = sender.open(); !opened)
        co_return std::unexpected(opened.error());
    const auto destination = receiver.native_socket().local_endpoint();
    if (!destination)
        co_return std::unexpected(destination.error());

    std::size_t sequence{};
    for (std::size_t round{}; round < rounds; ++round)
    {
        std::array<std::array<std::byte, sizeof(std::uint64_t)>, batch_size> payloads{};
        std::array<cnetmod::udp_send_datagram, batch_size> datagrams{};
        for (std::size_t index{}; index < batch_size; ++index)
        {
            const auto value = static_cast<std::uint64_t>(sequence + index);
            for (std::size_t byte{}; byte < sizeof(value); ++byte)
                payloads[index][byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xffU);
            datagrams[index] = cnetmod::udp_send_datagram{
                cnetmod::const_buffer{payloads[index].data(), payloads[index].size()},
                *destination};
        }
        const auto sent = co_await cnetmod::async_sendto_batch(
            context, sender.native_socket(), datagrams);
        if (!sent || *sent != datagrams.size())
            co_return std::unexpected(sent ? std::make_error_code(std::errc::io_error)
                                           : sent.error());

        const auto received = co_await cnetmod::async_recvfrom_batch(
            context, receiver.native_socket(), batch_size, 2048U);
        if (!received || received->size() != batch_size)
            co_return std::unexpected(received ? std::make_error_code(std::errc::io_error)
                                               : received.error());
        for (std::size_t index{}; index < received->size(); ++index)
        {
            if ((*received)[index].bytes.size() != sizeof(std::uint64_t))
                co_return std::unexpected(std::make_error_code(std::errc::message_size));
            std::uint64_t value{};
            for (std::size_t byte{}; byte < sizeof(value); ++byte)
                value |= static_cast<std::uint64_t>(
                             std::to_integer<unsigned>((*received)[index].bytes[byte]))
                    << (byte * 8U);
            if (value != sequence + index)
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        sequence += batch_size;
    }
    co_return {};
}

} // namespace

TEST(udp_batch_preserves_boundaries_and_backend_lifetime)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    cnetmod::cancel_token timeout_token;
    std::optional<std::expected<batch_probe_result, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await cnetmod::with_timeout(*context, std::chrono::seconds{5},
            run_udp_batch_probe(*context), timeout_token);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();

    ASSERT_TRUE(result.has_value());
    if (!result)
    {
        ASSERT_TRUE(false);
        return;
    }
    if (!*result)
    {
        ASSERT_EQ((*result).error().value(), 0);
        return;
    }
    ASSERT_EQ((*result)->received, std::size_t{4});
    ASSERT_TRUE((*result)->payloads_match);
#ifdef CNETMOD_PLATFORM_WINDOWS
    const bool require_rio = []
    {
        const auto* value = std::getenv("CNETMOD_REQUIRE_RIO");
        return value && std::string_view{value} != "0";
    }();
    // A developer machine is allowed to exercise the IOCP fallback when its
    // Winsock provider does not expose RIO.  The Windows CI gate opts in to
    // the stricter assertion so a fallback result can never be reported as
    // RIO coverage or used as a RIO performance datapoint.
    if (require_rio)
    {
        ASSERT_TRUE((*result)->rio_active);
        ASSERT_TRUE(!(*result)->rio_fallback);
        ASSERT_TRUE((*result)->rio_receive_completions >= 4U);
    }
    else
    {
        ASSERT_TRUE((*result)->rio_active || (*result)->rio_fallback);
        if ((*result)->rio_active)
            ASSERT_TRUE((*result)->rio_receive_completions >= 4U);
    }
#endif
}

TEST(udp_batch_wait_honors_deadline_cancellation)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::optional<std::expected<void, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await run_udp_batch_cancellation_probe(*context);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result && *result);
}

TEST(udp_batch_short_burst_does_not_wait_for_full_batch)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::optional<std::expected<void, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await run_udp_batch_short_burst_probe(*context);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result && *result);
}

TEST(udp_batch_iocp_fallback_preserves_order_under_pressure)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::optional<std::expected<void, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await run_udp_batch_ordering_stress(*context);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result && *result);
}

#ifdef CNETMOD_PLATFORM_WINDOWS
TEST(socket_release_rejects_live_async_backend)
{
    cnetmod::net_init network;
    auto created = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::datagram);
    ASSERT_TRUE(created.has_value());
    if (!created)
        return;

    created->set_async_state(std::make_shared<active_socket_backend>());
    const auto blocked = created->try_release();
    ASSERT_FALSE(blocked.has_value());
    if (!blocked)
        ASSERT_EQ(blocked.error(),
            cnetmod::make_error_code(cnetmod::errc::operation_in_progress));

    created->set_async_state({});
    const auto released = created->try_release();
    ASSERT_TRUE(released.has_value());
    if (released)
    {
        auto owner = cnetmod::socket::from_native(*released);
        owner.close();
    }
}
#endif

#ifdef CNETMOD_HAS_EPOLL
TEST(epoll_directional_waiters_preserve_read_write_cancel_and_hup)
{
    auto sockets = make_nonblocking_socket_pair();
    ASSERT_TRUE(sockets[0] >= 0 && sockets[1] >= 0);
    if (sockets[0] < 0 || sockets[1] < 0)
        return;

    {
        cnetmod::epoll_context context;
        bool read_completed{};
        bool write_completed{};
        auto read_waiter = await_epoll_direction(context, sockets[0], EPOLLIN,
            read_completed);
        auto write_waiter = await_epoll_direction(context, sockets[0], EPOLLOUT,
            write_completed);
        read_waiter.handle().resume();
        write_waiter.handle().resume();

        // EPOLLOUT is immediately ready, but it must not replace the armed
        // read waiter on the same fd.
        (void)context.poll();
        ASSERT_TRUE(write_completed);
        ASSERT_FALSE(read_completed);

        constexpr std::byte byte{0x42};
        ASSERT_EQ(::send(sockets[1], &byte, sizeof(byte), MSG_NOSIGNAL),
            static_cast<ssize_t>(sizeof(byte)));
        (void)context.run_one();
        ASSERT_TRUE(read_completed);

        std::byte received{};
        ASSERT_EQ(::recv(sockets[0], &received, sizeof(received), 0),
            static_cast<ssize_t>(sizeof(received)));

        // A one-shot read direction must be re-armable after its first wake.
        read_completed = false;
        auto rearmed_read = await_epoll_direction(context, sockets[0], EPOLLIN,
            read_completed);
        rearmed_read.handle().resume();
        ASSERT_EQ(::send(sockets[1], &byte, sizeof(byte), MSG_NOSIGNAL),
            static_cast<ssize_t>(sizeof(byte)));
        (void)context.run_one();
        ASSERT_TRUE(read_completed);
        ASSERT_EQ(::recv(sockets[0], &received, sizeof(received), 0),
            static_cast<ssize_t>(sizeof(received)));
    }

    {
        cnetmod::epoll_context context;
        bool cancelled_read{};
        bool surviving_write{};
        auto read_waiter = await_epoll_direction(context, sockets[0], EPOLLIN,
            cancelled_read);
        auto write_waiter = await_epoll_direction(context, sockets[0], EPOLLOUT,
            surviving_write);
        read_waiter.handle().resume();
        write_waiter.handle().resume();
        ASSERT_TRUE(context.remove(sockets[0], EPOLLIN,
                               read_waiter.handle().address())
                .has_value());
        (void)context.poll();
        ASSERT_FALSE(cancelled_read);
        ASSERT_TRUE(surviving_write);
    }

    {
        cnetmod::epoll_context context;
        bool read_terminal{};
        bool write_terminal{};
        auto read_waiter = await_epoll_direction(context, sockets[0], EPOLLIN,
            read_terminal);
        auto write_waiter = await_epoll_direction(context, sockets[0], EPOLLOUT,
            write_terminal);
        read_waiter.handle().resume();
        write_waiter.handle().resume();
        ASSERT_EQ(::close(sockets[1]), 0);
        sockets[1] = -1;
        // Some kernels report the already-writable direction before HUP.
        // Poll twice so the assertion covers both independent wake paths.
        (void)context.poll();
        (void)context.poll();
        ASSERT_TRUE(read_terminal);
        ASSERT_TRUE(write_terminal);
    }

    if (sockets[0] >= 0)
        ASSERT_EQ(::close(sockets[0]), 0);
}
#endif

RUN_TESTS()
