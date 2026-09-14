#include "test_framework.hpp"
#include <netdb.h>

import std;
import cnetmod.core.dns;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.error;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;

struct addrinfo;
extern "C" int __real_getaddrinfo(const char*, const char*, const addrinfo*, addrinfo**);

namespace {
std::atomic<bool> entered{};
std::atomic<bool> released{};
std::atomic<bool> returned{};
} // namespace

extern "C" int __wrap_getaddrinfo(const char* host, const char* service,
    const addrinfo* hints, addrinfo** result)
{
    if (host && std::string_view{host} == "cnetmod-dual-loopback.invalid")
    {
        const auto first = __real_getaddrinfo("127.0.0.1", service, hints, result);
        if (first != 0)
            return first;
        addrinfo* second{};
        const auto status = __real_getaddrinfo("127.0.0.2", service, hints, &second);
        if (status != 0)
        {
            freeaddrinfo(*result);
            *result = nullptr;
            return status;
        }
        auto* tail = *result;
        while (tail->ai_next)
            tail = tail->ai_next;
        tail->ai_next = second;
        return 0;
    }
    if (host && std::string_view{host} == "cnetmod-delayed.invalid")
    {
        entered.store(true, std::memory_order_release);
        released.wait(false, std::memory_order_acquire);
        const auto status = __real_getaddrinfo("localhost", service, hints, result);
        returned.store(true, std::memory_order_release);
        return status;
    }
    return __real_getaddrinfo(host, service, hints, result);
}

TEST(cancelled_dns_outlives_context_without_releasing_capacity_early)
{
    cnetmod::net_init network;

    struct cleanup
    {
        ~cleanup()
        {
            released.store(true, std::memory_order_release);
            released.notify_all();
            cnetmod::configure_dns_cache({});
        }
    } guard;

    cnetmod::configure_dns_cache({.enabled = false, .max_pending_lookups = 1});
    bool cancelled{};
    bool capacity_rejected{};
    {
        auto io = cnetmod::make_io_context();
        cnetmod::cancel_token token;
        unsigned completed{};
        auto finish = [&]
        {
            if (++completed == 2)
                io->stop();
        };
        auto lookup = [&]() -> cnetmod::task<void>
        {
            const auto result = co_await cnetmod::async_connect_happy_eyeballs(
                *io, "cnetmod-delayed.invalid", 1, {}, token);
            cancelled = !result && token.is_cancelled();
            finish();
        };
        auto cancel = [&]() -> cnetmod::task<void>
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_TRUE(entered.load());
            token.cancel();
            cnetmod::cancel_token second_token;
            const auto before = cnetmod::get_dns_cache_metrics().rejected_lookups;
            const auto second = co_await cnetmod::async_connect_happy_eyeballs(
                *io, "localhost", 1, {}, second_token);
            capacity_rejected = !second && cnetmod::get_dns_cache_metrics().rejected_lookups == before + 1;
            finish();
        };
        cnetmod::spawn(*io, lookup());
        cnetmod::spawn(*io, cancel());
        io->run();
        ASSERT_EQ(cnetmod::get_dns_cache_metrics().pending_lookups, 1U);
    }
    ASSERT_TRUE(cancelled);
    ASSERT_TRUE(capacity_rejected);
    ASSERT_FALSE(returned.load());
    released.store(true, std::memory_order_release);
    released.notify_all();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (cnetmod::get_dns_cache_metrics().pending_lookups != 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    ASSERT_TRUE(returned.load());
    ASSERT_EQ(cnetmod::get_dns_cache_metrics().pending_lookups, 0U);
}

TEST(happy_eyeballs_winner_settles_delayed_attempt_before_context_destruction)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::cancel_token token;
    const auto started = std::chrono::steady_clock::now();
    auto connect = [&]() -> cnetmod::task<void>
    {
        const auto result = co_await cnetmod::async_connect_happy_eyeballs(*io,
            "cnetmod-dual-loopback.invalid", endpoint->port(),
            {.fallback_delay = std::chrono::seconds{10}}, token);
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(result->metrics.resolved_address_count, 2U);
        ASSERT_EQ(result->metrics.attempted_count, 1U);
        ASSERT_FALSE(token.pending_.load());
        io->stop();
    };
    cnetmod::spawn(*io, connect());
    io->run();
    ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}

TEST(happy_eyeballs_cancellation_settles_pending_fallback)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::cancel_token token;
    const auto failures = cnetmod::get_dns_cache_metrics().connection_failures;
    const auto started = std::chrono::steady_clock::now();
    unsigned completed{};
    auto finish = [&]
    {
        if (++completed == 2)
            io->stop();
    };
    auto connect = [&]() -> cnetmod::task<void>
    {
        const auto result = co_await cnetmod::async_connect_happy_eyeballs(*io,
            "cnetmod-dual-loopback.invalid", endpoint->port(),
            {.fallback_delay = std::chrono::seconds{10}}, token);
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        ASSERT_FALSE(token.pending_.load());
        finish();
    };
    auto cancel = [&]() -> cnetmod::task<void>
    {
        while (cnetmod::get_dns_cache_metrics().connection_failures == failures &&
            std::chrono::steady_clock::now() - started < std::chrono::seconds{1})
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(cnetmod::get_dns_cache_metrics().connection_failures > failures);
        token.cancel();
        finish();
    };
    cnetmod::spawn(*io, connect());
    cnetmod::spawn(*io, cancel());
    io->run();
    ASSERT_EQ(completed, 2U);
    ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}

RUN_TESTS()
