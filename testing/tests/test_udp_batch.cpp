#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.coro.awaitable;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.udp;

#ifdef CNETMOD_HAS_IO_URING
    #include <arpa/inet.h>
    #include <errno.h>
    #include <liburing.h>
    #include <linux/udp.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
import cnetmod.io.platform.io_uring;
#endif

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

auto run_udp_batch_layout_case(cnetmod::io_context& context,
    std::string_view loopback_text, std::span<const std::size_t> sizes,
    bool mixed_peers, bool disable_checksum = false, bool segmented = false)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    const auto loopback = cnetmod::ip_address::from_string(loopback_text);
    if (!loopback)
        co_return std::unexpected(loopback.error());

    std::array<cnetmod::udp::udp_socket, 2> receivers{
        cnetmod::udp::udp_socket{context}, cnetmod::udp::udp_socket{context}};
    std::array<cnetmod::endpoint, 2> destinations{};
    for (std::size_t index{}; index < receivers.size(); ++index)
    {
        const auto opened = receivers[index].open(cnetmod::endpoint{*loopback, 0U},
            cnetmod::socket_options{.recv_buffer_size = 256 * 1024});
        if (!opened)
            co_return std::unexpected(opened.error());
        const auto destination = receivers[index].native_socket().local_endpoint();
        if (!destination)
            co_return std::unexpected(destination.error());
        destinations[index] = *destination;
    }
    cnetmod::udp::udp_socket sender{context};
    if (const auto opened = sender.open(loopback->family()); !opened)
        co_return std::unexpected(opened.error());
    // This layout fixture sends individual 24,000-byte datagrams. Darwin's default
    // UDP SO_SNDBUF is only 9216 bytes and rejects them with EMSGSIZE before
    // batch submission can be exercised. Provision one full-size datagram on
    // this test socket; keep the payloads, packet boundaries and assertions.
    if (const auto configured = sender.native_socket().apply_options(
            cnetmod::socket_options{.send_buffer_size = 64 * 1024}); !configured)
        co_return std::unexpected(configured.error());
#ifdef CNETMOD_HAS_IO_URING
    if (disable_checksum)
    {
        // Native socket options must retain their ordinary IPv4 datagram
        // behavior when the caller uses the batch API.
        const int enabled = 1;
        if (::setsockopt(static_cast<int>(sender.native_socket().native_handle()),
                SOL_SOCKET, SO_NO_CHECK, &enabled, sizeof(enabled)) != 0)
            co_return std::unexpected(cnetmod::make_error_code(cnetmod::from_native_error(errno)));
    }
#else
    (void)disable_checksum;
#endif

    std::vector<std::vector<std::byte>> payloads(sizes.size());
    std::vector<cnetmod::udp_send_datagram> datagrams;
    datagrams.reserve(sizes.size());
    for (std::size_t index{}; index < sizes.size(); ++index)
    {
        auto& payload = payloads[index];
        payload.resize(sizes[index]);
        for (std::size_t offset{}; offset < payload.size(); ++offset)
            payload[offset] = static_cast<std::byte>((index * 31U + offset) & 0xffU);
        const auto peer_index = mixed_peers ? (index / 5U) % receivers.size() : 0U;
        datagrams.push_back({cnetmod::const_buffer{payload.data(), payload.size()},
            destinations[peer_index]});
    }

    std::size_t submitted{};
#ifdef CNETMOD_HAS_IO_URING
    cnetmod::cancel_token segmented_token;
#else
    (void)segmented;
#endif
    while (submitted < datagrams.size())
    {
#ifdef CNETMOD_HAS_IO_URING
        const auto sent = segmented
            ? co_await cnetmod::async_sendto_segmented_batch(context,
                  sender.native_socket(), std::span<const cnetmod::udp_send_datagram>{datagrams}.subspan(submitted), segmented_token)
            : co_await cnetmod::async_sendto_batch(context,
                  sender.native_socket(), std::span<const cnetmod::udp_send_datagram>{datagrams}.subspan(submitted));
#else
        const auto sent = co_await cnetmod::async_sendto_batch(context,
            sender.native_socket(), std::span<const cnetmod::udp_send_datagram>{datagrams}.subspan(submitted));
#endif
        if (!sent)
        {
#ifdef CNETMOD_HAS_IO_URING
            const auto current_errno = errno;
#else
            const auto current_errno = 0;
#endif
            logger::error{"UDP batch send error: family={} datagrams={} mixed_peers={} no_checksum={} submitted={} category={} value={} message={} current_errno={}",
                loopback_text, sizes.size(), mixed_peers, disable_checksum,
                submitted, sent.error().category().name(), sent.error().value(),
                sent.error().message(), current_errno};
            co_return std::unexpected(sent.error());
        }
        if (*sent == 0U || *sent > datagrams.size() - submitted)
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
#ifdef CNETMOD_HAS_IO_URING
        if (*sent > 16U)
            co_return std::unexpected(std::make_error_code(std::errc::value_too_large));
#endif
        submitted += *sent;
    }

    // A sentinel after the batch also detects a duplicated final datagram:
    // matching only the expected count could otherwise leave it unread.
    constexpr std::array sentinel{std::byte{0xa5}, std::byte{0x5a}};
    const auto peer_count = mixed_peers ? receivers.size() : std::size_t{1};
    for (std::size_t peer_index{}; peer_index < peer_count; ++peer_index)
    {
        const auto sent = co_await cnetmod::async_sendto(context,
            sender.native_socket(), cnetmod::const_buffer{sentinel.data(), sentinel.size()},
            destinations[peer_index]);
        if (!sent)
        {
            logger::error{"UDP batch sentinel send error: family={} datagrams={} mixed_peers={} no_checksum={} destination={} category={} value={} message={}",
                loopback_text, sizes.size(), mixed_peers, disable_checksum,
                destinations[peer_index].to_string(), sent.error().category().name(),
                sent.error().value(), sent.error().message()};
            co_return std::unexpected(sent.error());
        }
    }

    std::vector<std::byte> received(*std::max_element(sizes.begin(), sizes.end()) + 1U);
    cnetmod::cancel_token receive_token;
    for (std::size_t peer_index{}; peer_index < peer_count; ++peer_index)
    {
        for (std::size_t index{}; index <= sizes.size(); ++index)
        {
            if (index < sizes.size() &&
                (mixed_peers ? (index / 5U) % receivers.size() : 0U) != peer_index)
                continue;
            cnetmod::endpoint source;
            const auto count = co_await cnetmod::with_timeout(context,
                std::chrono::seconds{2}, cnetmod::async_recvfrom(context,
                    receivers[peer_index].native_socket(),
                    cnetmod::mutable_buffer{received.data(), received.size()}, source,
                    receive_token), receive_token);
            if (!count)
            {
                logger::error{"UDP batch receive error: family={} datagrams={} mixed_peers={} no_checksum={} destination={} index={} category={} value={} message={}",
                    loopback_text, sizes.size(), mixed_peers, disable_checksum,
                    destinations[peer_index].to_string(), index, count.error().category().name(),
                    count.error().value(), count.error().message()};
                co_return std::unexpected(count.error());
            }
            const auto expected = index == sizes.size()
                ? std::span<const std::byte>{sentinel}
                : std::span<const std::byte>{payloads[index]};
            if (*count != expected.size() ||
                !std::ranges::equal(std::span<const std::byte>{received.data(), *count}, expected))
            {
                const auto compared_size = std::min(*count, expected.size());
                std::size_t different_offset{};
                while (different_offset < compared_size &&
                    received[different_offset] == expected[different_offset])
                    ++different_offset;
                logger::error{"UDP batch mismatch: family={} datagrams={} mixed_peers={} no_checksum={} receiver={} destination={} source={} index={} received={} expected={} first_difference={} received_byte={} expected_byte={}",
                    loopback_text, sizes.size(), mixed_peers, disable_checksum,
                    peer_index, destinations[peer_index].to_string(), source.to_string(),
                    index, *count, expected.size(), different_offset,
                    different_offset < *count ? std::to_integer<unsigned>(received[different_offset]) : 256U,
                    different_offset < expected.size() ? std::to_integer<unsigned>(expected[different_offset]) : 256U};
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }
    }
    co_return {};
}

auto run_udp_batch_layout_probe(cnetmod::io_context& context,
    std::string_view loopback_text, bool segmented = false)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    const bool constrained_wsl_ipv4 =
        std::getenv("WSL_DISTRO_NAME") != nullptr && loopback_text == "127.0.0.1";
    const auto full_payload_size = constrained_wsl_ipv4 ? 1400U : 1472U;
    std::array<std::size_t, 16> equal_sizes{};
    equal_sizes.fill(full_payload_size);
    if (auto result = co_await run_udp_batch_layout_case(context, loopback_text,
            equal_sizes, false, false, segmented);
        !result)
        co_return result;
#ifdef CNETMOD_HAS_IO_URING
    if (loopback_text == "127.0.0.1")
    {
        if (auto result = co_await run_udp_batch_layout_case(context, loopback_text,
                equal_sizes, false, true, segmented);
            !result)
            co_return result;
    }
#endif

    // Multiple peers and >16 inputs force prefix accounting across calls.
    // Short packets followed by full-size packets and zero-byte messages
    // must retain their individual lengths, payloads and destination order.
    std::array<std::size_t, 40> mixed_sizes{};
    mixed_sizes.fill(full_payload_size);
    mixed_sizes[2] = 17U;
    mixed_sizes[10] = 0U;
    mixed_sizes[13] = 1U;
    mixed_sizes[24] = 0U;
    mixed_sizes[37] = 23U;
    if (auto result = co_await run_udp_batch_layout_case(context, loopback_text,
            mixed_sizes, true, false, segmented);
        !result)
        co_return result;

    // Verify independent datagrams whose aggregate exceeds 65507 bytes.
    // Mirrored WSL policy-routes IPv4 localhost through a 1500-MTU host-facing
    // adapter, so retain the aggregate-overflow contract there with safe-sized
    // datagrams. IPv6 localhost and native Linux continue to verify 24 KiB
    // individual payloads as well as an aggregate above the UDP ceiling.
    if (constrained_wsl_ipv4)
    {
        std::array<std::size_t, 48> aggregate_sizes{};
        aggregate_sizes.fill(1400U);
        co_return co_await run_udp_batch_layout_case(context, loopback_text,
            aggregate_sizes, false, false, segmented);
    }
    constexpr std::array<std::size_t, 3> large_sizes{24000U, 24000U, 24000U};
    co_return co_await run_udp_batch_layout_case(context, loopback_text,
        large_sizes, false, false, segmented);
}

#ifdef CNETMOD_HAS_IO_URING
auto run_udp_batch_partial_prefix_probe(cnetmod::io_context& context,
    bool cancellable = false, bool segmented = false)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    if (!loopback)
        co_return std::unexpected(loopback.error());
    cnetmod::udp::udp_socket receiver{context};
    if (const auto opened = receiver.open(cnetmod::endpoint{*loopback, 0U}); !opened)
        co_return std::unexpected(opened.error());
    const auto peer = receiver.native_socket().local_endpoint();
    if (!peer)
        co_return std::unexpected(peer.error());
    cnetmod::udp::udp_socket sender{context};
    if (const auto opened = sender.open(); !opened)
        co_return std::unexpected(opened.error());

    std::array<std::array<std::byte, 1472>, 5> payloads{};
    std::array<cnetmod::udp_send_datagram, 5> datagrams{};
    for (std::size_t index{}; index < datagrams.size(); ++index)
    {
        payloads[index].fill(static_cast<std::byte>(index));
        // The first three datagrams are valid, then destination port zero
        // makes sendmmsg return the accepted prefix before the EINVAL error.
        datagrams[index] = {cnetmod::const_buffer{payloads[index].data(), payloads[index].size()},
            index < 3U ? *peer : cnetmod::endpoint{*loopback, 0U}};
    }
    cnetmod::cancel_token send_token;
    const auto send_batch = [&](std::span<const cnetmod::udp_send_datagram> batch)
        -> cnetmod::task<std::expected<std::size_t, std::error_code>>
    {
        if (segmented)
            return cnetmod::async_sendto_segmented_batch(context, sender.native_socket(), batch, send_token);
        return cancellable
            ? cnetmod::async_sendto_batch(context, sender.native_socket(), batch, send_token)
            : cnetmod::async_sendto_batch(context, sender.native_socket(), batch);
    };
    const auto sent = co_await send_batch(datagrams);
    if (!sent || *sent != 3U)
        co_return std::unexpected(sent ? std::make_error_code(std::errc::protocol_error) : sent.error());
    const auto rejected = co_await send_batch(
        std::span<const cnetmod::udp_send_datagram>{datagrams}.subspan(*sent));
    if (rejected || rejected.error() != cnetmod::make_error_code(cnetmod::errc::invalid_argument))
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    if (cancellable)
    {
        send_token.cancel();
        const auto cancelled = co_await send_batch(datagrams);
        if (cancelled || cancelled.error() !=
                cnetmod::make_error_code(cnetmod::errc::operation_aborted))
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    // A distinct trailing packet proves the accepted prefix was not resent;
    // checking just three receives could otherwise leave duplicates unread.
    constexpr std::array sentinel{std::byte{0xa5}, std::byte{0x5a}};
    if (segmented)
    {
        const auto sent_sentinel = co_await cnetmod::async_sendto(context,
            sender.native_socket(), cnetmod::const_buffer{sentinel.data(), sentinel.size()}, *peer);
        if (!sent_sentinel || *sent_sentinel != sentinel.size())
            co_return std::unexpected(sent_sentinel ? std::make_error_code(std::errc::protocol_error) : sent_sentinel.error());
    }
    std::array<std::byte, 1473> received{};
    cnetmod::cancel_token token;
    for (std::size_t index{}; index < (segmented ? 4U : 3U); ++index)
    {
        cnetmod::endpoint source;
        const auto count = co_await cnetmod::with_timeout(context, std::chrono::seconds{2},
            cnetmod::async_recvfrom(context, receiver.native_socket(),
                cnetmod::mutable_buffer{received.data(), received.size()}, source, token), token);
        if (!count)
            co_return std::unexpected(count.error());
        const auto expected = index == 3U
            ? std::span<const std::byte>{sentinel}
            : std::span<const std::byte>{payloads[index]};
        if (*count != expected.size() ||
            !std::ranges::equal(std::span<const std::byte>{received.data(), *count}, expected))
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    co_return {};
}
#endif

#ifdef CNETMOD_PLATFORM_WINDOWS
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
#endif

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

TEST(udp_batch_layout_preserves_ipv4_and_ipv6_boundaries)
{
    cnetmod::net_init network;
    logger::init("udp-batch-layout", logger::level::error);
    for (const auto loopback : {std::string_view{"127.0.0.1"}, std::string_view{"::1"}})
    {
        auto context = cnetmod::make_io_context();
        std::optional<std::expected<void, std::error_code>> result;
        auto wrapper = [&]() -> cnetmod::task<void>
        {
            result = co_await run_udp_batch_layout_probe(*context, loopback);
            context->stop();
        };
        auto running = wrapper();
        running.handle().resume();
        context->run();
        logger::flush();
        ASSERT_TRUE(result.has_value());
        if (result && !*result)
            ASSERT_EQ(result->error().value(), 0);
        ASSERT_TRUE(result && *result);
    }
    logger::shutdown();
}

#ifdef CNETMOD_HAS_IO_URING
TEST(udp_batch_partial_prefix_and_suffix_error_are_preserved)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::optional<std::expected<void, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await run_udp_batch_partial_prefix_probe(*context);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();
    ASSERT_TRUE(result.has_value());
    if (result && !*result)
        ASSERT_EQ(result->error().value(), 0);
    ASSERT_TRUE(result && *result);
}

TEST(udp_batch_cancel_token_preserves_partial_prefix_and_stops_new_submission)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::optional<std::expected<void, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await run_udp_batch_partial_prefix_probe(*context, true);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();
    ASSERT_TRUE(result.has_value());
    if (result && !*result)
        ASSERT_EQ(result->error().value(), 0);
    ASSERT_TRUE(result && *result);
}

TEST(udp_send_prepared_operation_survives_deferred_submission_error)
{
    cnetmod::net_init network;
    cnetmod::io_uring_context context;
    cnetmod::udp::udp_socket receiver{context};
    cnetmod::udp::udp_socket sender{context};
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    ASSERT_TRUE(loopback.has_value());
    if (!loopback)
        return;
    ASSERT_TRUE(receiver.open(cnetmod::endpoint{*loopback, 0U}).has_value());
    ASSERT_TRUE(sender.open().has_value());
    const auto destination = receiver.native_socket().local_endpoint();
    ASSERT_TRUE(destination.has_value());
    if (!destination)
        return;

    // Keep the ring object alive through a duplicate descriptor, but make one
    // real io_uring_enter fail with EBADF. Only this test owns/uses the ring.
    const int ring_fd = context.native_ring()->ring_fd;
    const int saved_fd = ::dup(ring_fd);
    ASSERT_TRUE(saved_fd >= 0);
    if (saved_fd < 0)
        return;
    for (unsigned index = 1; index < context.submit_batch_size(); ++index)
    {
        auto* sqe = context.prepare_sqe();
        ASSERT_TRUE(sqe != nullptr);
        if (!sqe)
        {
            (void)::close(saved_fd);
            return;
        }
        ::io_uring_prep_nop(sqe);
        ::io_uring_sqe_set_data(sqe, nullptr);
    }
    constexpr std::string_view payload{"deferred send retains its buffer"};
    std::array<char, 128> received_bytes{};
    std::optional<std::expected<std::size_t, std::error_code>> sent;
    std::optional<std::expected<std::size_t, std::error_code>> received;
    cnetmod::endpoint source;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        sent = co_await cnetmod::async_sendto(context, sender.native_socket(),
            cnetmod::const_buffer{payload.data(), payload.size()}, *destination);
        if (sent && *sent)
            received = co_await cnetmod::async_recvfrom(context, receiver.native_socket(),
                cnetmod::mutable_buffer{received_bytes.data(), received_bytes.size()}, source);
        context.stop();
    };
    auto running = wrapper();
    const int closed = ::close(ring_fd);
    ASSERT_EQ(closed, 0);
    if (closed != 0)
    {
        (void)::close(saved_fd);
        return;
    }
    running.handle().resume();
    // Restore before assertions or reactor work can create another descriptor.
    const int restored = ::dup2(saved_fd, ring_fd);
    ASSERT_EQ(restored, ring_fd);
    (void)::close(saved_fd);
    if (restored != ring_fd)
        return;
    ASSERT_FALSE(sent.has_value());
    ASSERT_EQ(context.submission_error(), (std::error_code{EBADF, std::generic_category()}));
    if (!context.stopped())
        context.run();
    ASSERT_TRUE(sent && *sent);
    ASSERT_TRUE(received && *received);
    if (sent && *sent)
        ASSERT_EQ(**sent, payload.size());
    if (received && *received)
        ASSERT_EQ((std::string_view{received_bytes.data(), **received}), payload);
    ASSERT_FALSE(context.submission_error());
}

enum class batch_wait_probe
{
    writable,
    cancel,
    full_queue_cancel,
    accepted_prefix_error,
};

void check_udp_batch_would_block(batch_wait_probe probe, bool segmented = false)
{
    const bool cancel_send = probe == batch_wait_probe::cancel ||
        probe == batch_wait_probe::full_queue_cancel;
    const bool fill_submission_queue = probe == batch_wait_probe::full_queue_cancel;
    const bool fail_suffix = probe == batch_wait_probe::accepted_prefix_error;
    cnetmod::net_init network;
    // UDP loopback normally drops at the receiver instead of backpressuring
    // the sender. A real connected TCP pair deterministically exercises this
    // socket-based batch API's common EAGAIN -> asynchronous-send branch.
    // AF_UNIX streams cannot substitute: they reject its nonempty msg_name.
    const auto listener_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_TRUE(listener_fd >= 0);
    if (listener_fd < 0)
        return;
    auto listener = cnetmod::socket::from_native(listener_fd);
    ::sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    const int receive_buffer_size = 4096;
    ASSERT_EQ(::setsockopt(listener_fd, SOL_SOCKET, SO_RCVBUF,
                  &receive_buffer_size, sizeof(receive_buffer_size)), 0);
    const auto bound = ::bind(listener_fd, reinterpret_cast<const ::sockaddr*>(&address), sizeof(address));
    ASSERT_EQ(bound, 0);
    if (bound != 0)
        return;
    const auto listening = ::listen(listener_fd, 1);
    ASSERT_EQ(listening, 0);
    if (listening != 0)
        return;
    ::socklen_t address_size = sizeof(address);
    ASSERT_EQ(::getsockname(listener_fd, reinterpret_cast<::sockaddr*>(&address), &address_size), 0);

    const auto sender_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_TRUE(sender_fd >= 0);
    if (sender_fd < 0)
        return;
    auto sender = cnetmod::socket::from_native(sender_fd);
    const auto connected = ::connect(sender_fd, reinterpret_cast<const ::sockaddr*>(&address), sizeof(address));
    ASSERT_EQ(connected, 0);
    if (connected != 0)
        return;
    const auto receiver_fd = ::accept4(listener_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    ASSERT_TRUE(receiver_fd >= 0);
    if (receiver_fd < 0)
        return;
    auto receiver = cnetmod::socket::from_native(receiver_fd);
    ASSERT_TRUE(sender.apply_options(cnetmod::socket_options{.send_buffer_size = 4096}).has_value());

    const auto peer = sender.remote_endpoint();
    ASSERT_TRUE(peer.has_value());
    if (!peer)
        return;
    constexpr std::string_view payload{"ready"};
    constexpr std::string_view suffix_payload{"suffix"};
    const std::array datagram_storage{
        cnetmod::udp_send_datagram{cnetmod::const_buffer{payload.data(), payload.size()}, *peer},
        cnetmod::udp_send_datagram{cnetmod::const_buffer{suffix_payload.data(), suffix_payload.size()}, *peer}};
    const auto datagrams = std::span<const cnetmod::udp_send_datagram>{datagram_storage}
        .first(fail_suffix ? 2U : 1U);
    std::unique_ptr<cnetmod::io_context> context;
    if (fill_submission_queue || fail_suffix)
        context = std::make_unique<cnetmod::io_uring_context>(fill_submission_queue ? 8U : 256U);
    else
        context = cnetmod::make_io_context();
    cnetmod::cancel_token send_token;
    std::optional<std::expected<std::size_t, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        if (segmented)
            result = co_await cnetmod::async_sendto_segmented_batch(*context, sender, datagrams, send_token);
        else if (cancel_send)
            result = co_await cnetmod::async_sendto_batch(*context, sender, datagrams, send_token);
        else
            result = co_await cnetmod::async_sendto_batch(*context, sender, datagrams);
        context->stop();
    };
    auto running = wrapper();
    std::array<std::byte, 4096> filler{};
    bool would_block{};
    for (std::size_t attempt{}; attempt < 4096U; ++attempt)
    {
        const auto sent = ::send(sender_fd, filler.data(), filler.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent < 0)
        {
            would_block = errno == EAGAIN || errno == EWOULDBLOCK;
            break;
        }
    }
    ASSERT_TRUE(would_block);
    if (!would_block)
        return;
    // A failed 4 KiB write does not prove stable backpressure: ACK processing
    // can briefly reopen a few bytes in the sender while the receiver's window
    // is still closing. Fill the tail one byte at a time and require a sustained
    // EAGAIN interval. This keeps the five-byte probe pending until the test
    // deliberately drains the peer instead of racing TCP window updates.
    const std::byte final_fill{};
    constexpr std::size_t stable_eagain_count = 32U;
    std::size_t consecutive_eagain{};
    const auto saturation_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{2};
    while (consecutive_eagain < stable_eagain_count &&
        std::chrono::steady_clock::now() < saturation_deadline)
    {
        const auto sent = ::send(sender_fd, &final_fill, sizeof(final_fill),
            MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent == static_cast<ssize_t>(sizeof(final_fill)))
        {
            consecutive_eagain = 0U;
            continue;
        }
        ASSERT_TRUE(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        if (sent >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            break;
        ++consecutive_eagain;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    would_block = consecutive_eagain == stable_eagain_count;
    ASSERT_TRUE(would_block);
    if (!would_block)
        return;
    running.handle().resume();
    // Comparing the helper's cnetmod-category would_block error against a
    // generic-category std::errc would incorrectly complete with an error here.
    ASSERT_FALSE(result.has_value());
    if (fail_suffix)
    {
        auto& uring = static_cast<cnetmod::io_uring_context&>(*context);
        bool first_accepted{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        // No worker runs this test-owned ring. Dispatch real EAGAIN/POLLOUT
        // completions one at a time until the first SENDMSG succeeds, then
        // retain that CQE so the batch cannot attempt its suffix yet.
        while (!first_accepted && !result && std::chrono::steady_clock::now() < deadline)
        {
            while (::recv(receiver_fd, filler.data(), filler.size(), MSG_DONTWAIT) > 0) {}
            (void)uring.flush_pending();
            ::io_uring_cqe* completion{};
            ::__kernel_timespec timeout{0, 100000000};
            const auto waited = ::io_uring_wait_cqe_timeout(uring.native_ring(), &completion, &timeout);
            if (waited == -ETIME || waited == -EINTR)
                continue;
            ASSERT_EQ(waited, 0);
            if (waited != 0)
                break;
            auto* operation = static_cast<cnetmod::uring_overlapped*>(::io_uring_cqe_get_data(completion));
            const auto native_result = completion->res;
            if (operation && native_result == static_cast<int>(payload.size()))
            {
                // POLLOUT cannot report five (POLLIN | POLLOUT) when only
                // POLLOUT was requested. This is the real five-byte SENDMSG.
                first_accepted = true;
                break;
            }
            ::io_uring_cqe_seen(uring.native_ring(), completion);
            ASSERT_TRUE(operation != nullptr);
            if (!operation)
                continue;
            ASSERT_TRUE(operation->completion == nullptr);
            operation->result = native_result;
            operation->coroutine.resume();
        }
        ASSERT_TRUE(first_accepted);
        ASSERT_FALSE(result.has_value());
        // This happens after kernel acceptance, before the real dispatcher
        // resumes the batch. The next ordinary sendmmsg must return EPIPE.
        ASSERT_EQ(::shutdown(sender_fd, SHUT_WR), 0);
    }
    else if (cancel_send)
    {
        // Give the queued SENDMSG an event-loop turn while the queue stays
        // full. It may remain in flight or park on POLLOUT after EAGAIN;
        // cancellation must finish either state without a receiver draining it.
        (void)context->poll();
        ASSERT_FALSE(result.has_value());
        if (fill_submission_queue)
        {
            // Fill the real SQ on its owner immediately before dispatching
            // cancellation. A cancellation adapter which drops a full SQ hangs
            // here because the TCP peer is deliberately never drained.
            struct cancellation_fixture
            {
                cnetmod::io_uring_context& context;
                cnetmod::cancel_token& token;
                bool filled = false;
                std::size_t entries = 0;
            } fixture{static_cast<cnetmod::io_uring_context&>(*context), send_token};
            context->post([](void* argument)
            {
                auto& state = *static_cast<cancellation_fixture*>(argument);
                while (auto* sqe = state.context.prepare_sqe())
                {
                    ::io_uring_prep_nop(sqe);
                    ::io_uring_sqe_set_data(sqe, nullptr);
                    ++state.entries;
                }
                state.filled = ::io_uring_sq_space_left(state.context.native_ring()) == 0;
                state.token.cancel();
            }, &fixture);
            context->run();
            ASSERT_TRUE(fixture.filled);
            ASSERT_TRUE(fixture.entries > 0U);
        }
        else
        {
            send_token.cancel();
        }
    }
    else
    {
        while (::recv(receiver_fd, filler.data(), filler.size(), MSG_DONTWAIT) > 0) {}
    }
    if (!context->stopped())
        context->run();
    ASSERT_TRUE(result.has_value());
    if (cancel_send)
    {
        ASSERT_TRUE(result && !*result);
        if (result && !*result)
            ASSERT_EQ(result->error(), cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        ASSERT_FALSE(send_token.pending_.load(std::memory_order_acquire));
        if (fill_submission_queue && result && !*result)
        {
            // Permit operation/frame address reuse only after cancellation has
            // fully retired. No late cancellation may target the next send.
            running = {};
            send_token.reset();
            context->restart();
            result.reset();
            while (::recv(receiver_fd, filler.data(), filler.size(), MSG_DONTWAIT) > 0) {}
            auto reused = wrapper();
            reused.handle().resume();
            if (!context->stopped())
                context->run();
            ASSERT_TRUE(result && *result);
            if (result && *result)
                ASSERT_EQ(**result, std::size_t{1});
            ASSERT_FALSE(send_token.pending_.load(std::memory_order_acquire));
        }
    }
    else
    {
        ASSERT_TRUE(result && *result);
        if (result && *result)
            ASSERT_EQ(**result, std::size_t{1});
        if (fail_suffix)
        {
            std::optional<std::expected<std::size_t, std::error_code>> suffix_result;
            auto retry = [&]() -> cnetmod::task<void>
            {
                suffix_result = co_await cnetmod::async_sendto_batch(*context, sender, datagrams.subspan(1));
            };
            auto retrying = retry();
            retrying.handle().resume();
            ASSERT_TRUE(suffix_result.has_value());
            ASSERT_TRUE(suffix_result && !*suffix_result);
            if (suffix_result && !*suffix_result)
                ASSERT_EQ(suffix_result->error(), cnetmod::make_error_code(cnetmod::errc::broken_pipe));
        }
    }
}

TEST(udp_batch_would_block_enters_async_send_path)
{
    check_udp_batch_would_block(batch_wait_probe::writable);
}

TEST(udp_batch_cancel_token_interrupts_blocked_send_without_draining_peer)
{
    check_udp_batch_would_block(batch_wait_probe::cancel);
}

TEST(udp_batch_cancel_token_survives_full_submission_queue_and_reuse)
{
    check_udp_batch_would_block(batch_wait_probe::full_queue_cancel);
}

TEST(udp_batch_async_first_preserves_prefix_when_suffix_fails)
{
    check_udp_batch_would_block(batch_wait_probe::accepted_prefix_error);
}
#endif

#ifdef CNETMOD_PLATFORM_WINDOWS
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

#ifdef CNETMOD_HAS_IO_URING
namespace {
auto run_udp_gro_probe(cnetmod::io_context& context)
    -> cnetmod::task<std::expected<bool, std::error_code>>
{
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    if (!loopback)
        co_return std::unexpected(loopback.error());

    cnetmod::udp::udp_socket receiver{context};
    if (const auto opened = receiver.open(cnetmod::endpoint{*loopback, 0U},
            cnetmod::socket_options{.udp_receive_coalescing = true});
        !opened)
        co_return std::unexpected(opened.error());
    if (!receiver.native_socket().udp_receive_coalescing_enabled())
        co_return false;

    cnetmod::udp::udp_socket sender{context};
    if (const auto opened = sender.open(); !opened)
        co_return std::unexpected(opened.error());
    const auto peer = receiver.native_socket().local_endpoint();
    if (!peer)
        co_return std::unexpected(peer.error());
    ::sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(peer->port());
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(static_cast<int>(sender.native_socket().native_handle()),
            reinterpret_cast<const ::sockaddr*>(&destination), sizeof(destination)) != 0)
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::from_native_error(errno)));

    constexpr std::size_t segment_size = 64U;
    constexpr std::size_t segment_count = 4U;
    std::array<std::byte, segment_size * segment_count> payload{};
    for (std::size_t segment{}; segment < segment_count; ++segment)
        std::fill_n(payload.begin() + static_cast<std::ptrdiff_t>(segment * segment_size),
            segment_size, static_cast<std::byte>(segment + 1U));

    ::iovec vector{payload.data(), payload.size()};
    alignas(::cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(std::uint16_t))> control{};
    ::msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1U;
    message.msg_control = control.data();
    message.msg_controllen = control.size();
    auto* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = IPPROTO_UDP;
    header->cmsg_type = UDP_SEGMENT;
    header->cmsg_len = CMSG_LEN(sizeof(std::uint16_t));
    const auto native_segment_size = static_cast<std::uint16_t>(segment_size);
    std::memcpy(CMSG_DATA(header), &native_segment_size, sizeof(native_segment_size));

    if (::sendmsg(static_cast<int>(sender.native_socket().native_handle()), &message, 0) !=
        static_cast<ssize_t>(payload.size()))
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::from_native_error(errno)));

    cnetmod::cancel_token token;
    const auto received = co_await cnetmod::with_timeout(context, std::chrono::seconds{2},
        cnetmod::async_recvfrom_batch(context, receiver.native_socket(), 8U, segment_size, token),
        token);
    if (!received)
        co_return std::unexpected(received.error());
    if (received->size() != 1U || received->front().segment_size != segment_size ||
        !std::ranges::equal(received->front().bytes, payload))
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    co_return true;
}

auto run_udp_segmented_native_options(cnetmod::io_context& context, bool cork)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
    if (!loopback)
        co_return std::unexpected(loopback.error());
    cnetmod::udp::udp_socket receiver{context};
    cnetmod::udp::udp_socket sender{context};
    if (auto opened = receiver.open(cnetmod::endpoint{*loopback, 0U}); !opened)
        co_return std::unexpected(opened.error());
    if (auto opened = sender.open(); !opened)
        co_return std::unexpected(opened.error());
    const auto peer = receiver.native_socket().local_endpoint();
    if (!peer)
        co_return std::unexpected(peer.error());
    const auto fd = static_cast<int>(sender.native_socket().native_handle());
    const int option = cork ? UDP_CORK : UDP_SEGMENT;
    const int enabled = cork ? 1 : 64;
    if (::setsockopt(fd, IPPROTO_UDP, option, &enabled, sizeof(enabled)) != 0)
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::from_native_error(errno)));
    std::array<std::byte, 128> first{}, second{};
    first.fill(std::byte{0x31});
    second.fill(std::byte{0x7e});
    const std::array datagrams{
        cnetmod::udp_send_datagram{cnetmod::const_buffer{first.data(), first.size()}, *peer},
        cnetmod::udp_send_datagram{cnetmod::const_buffer{second.data(), second.size()}, *peer}};
    cnetmod::cancel_token token;
    const auto sent = co_await cnetmod::async_sendto_segmented_batch(
        context, sender.native_socket(), datagrams, token);
    if (!sent || *sent != 2U)
        co_return std::unexpected(sent ? std::make_error_code(std::errc::protocol_error) : sent.error());
    int observed{};
    ::socklen_t size = sizeof(observed);
    if (::getsockopt(fd, IPPROTO_UDP, option, &observed, &size) != 0 || observed != enabled)
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    if (cork)
    {
        const int disabled{};
        if (::setsockopt(fd, IPPROTO_UDP, UDP_CORK, &disabled, sizeof(disabled)) != 0)
            co_return std::unexpected(cnetmod::make_error_code(cnetmod::from_native_error(errno)));
    }
    // Native CORK emits one combined256-byte UDP; native SEGMENT64 emits
    // four64-byte UDPs. Explicit optimization must retain either contract.
    std::array<std::byte, 257> bytes{};
    for (std::size_t index{}; index < (cork ? 1U : 4U); ++index)
    {
        cnetmod::endpoint source;
        const auto received = co_await cnetmod::with_timeout(context, std::chrono::seconds{2},
            cnetmod::async_recvfrom(context, receiver.native_socket(),
                cnetmod::mutable_buffer{bytes.data(), bytes.size()}, source, token), token);
        if (!received || *received != (cork ? 256U : 64U))
            co_return std::unexpected(received ? std::make_error_code(std::errc::protocol_error) : received.error());
        for (std::size_t offset{}; offset < *received; ++offset)
        {
            const auto expected = (cork ? offset < 128U : index < 2U)
                ? std::byte{0x31} : std::byte{0x7e};
            if (bytes[offset] != expected)
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
    }
    co_return {};
}
} // namespace

TEST(udp_gro_preserves_coalesced_storage_and_segment_boundaries)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::optional<std::expected<bool, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await run_udp_gro_probe(*context);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();
    ASSERT_TRUE(result.has_value());
    if (result && !*result)
        ASSERT_EQ(result->error().value(), 0);
    ASSERT_TRUE(result && *result);
}

TEST(udp_segmented_batch_retains_native_cork_and_segment_options)
{
    cnetmod::net_init network;
    for (const bool cork : {false, true})
    {
        auto context = cnetmod::make_io_context();
        std::optional<std::expected<void, std::error_code>> result;
        auto wrapper = [&]() -> cnetmod::task<void>
        {
            result = co_await run_udp_segmented_native_options(*context, cork);
            context->stop();
        };
        auto running = wrapper();
        running.handle().resume();
        context->run();
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result && *result);
    }
}

TEST(udp_segmented_batch_fallback_preserves_pending_cancel_full_queue_and_reuse)
{
    check_udp_batch_would_block(batch_wait_probe::writable, true);
    check_udp_batch_would_block(batch_wait_probe::cancel, true);
    check_udp_batch_would_block(batch_wait_probe::full_queue_cancel, true);
    check_udp_batch_would_block(batch_wait_probe::accepted_prefix_error, true);
}

TEST(udp_segmented_batch_preserves_ipv4_ipv6_tail_empty_peer_and_cap_boundaries)
{
    cnetmod::net_init network;
    for (const auto loopback : {std::string_view{"127.0.0.1"}, std::string_view{"::1"}})
    {
        auto context = cnetmod::make_io_context();
        std::optional<std::expected<void, std::error_code>> result;
        auto wrapper = [&]() -> cnetmod::task<void>
        {
            result = co_await run_udp_batch_layout_probe(*context, loopback, true);
            context->stop();
        };
        auto running = wrapper();
        running.handle().resume();
        context->run();
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result && *result);
    }
}

TEST(udp_segmented_batch_reports_original_prefix_and_preserves_early_cancel)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::optional<std::expected<void, std::error_code>> result;
    auto wrapper = [&]() -> cnetmod::task<void>
    {
        result = co_await run_udp_batch_partial_prefix_probe(*context, true, true);
        context->stop();
    };
    auto running = wrapper();
    running.handle().resume();
    context->run();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result && *result);
}
#endif

RUN_TESTS()
