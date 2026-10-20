#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IOCP
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <WinSock2.h>
#endif

#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

#ifdef CNETMOD_HAS_IOCP
namespace {

auto make_udp_socket() -> cnetmod::socket
{
    auto result = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::datagram);
    if (!result)
        throw std::system_error(result.error());
    return std::move(*result);
}

struct udp_fixture
{
    std::unique_ptr<cnetmod::io_context> socket_context = cnetmod::make_io_context();
    std::unique_ptr<cnetmod::io_context> resume_context = cnetmod::make_io_context();
    cnetmod::socket sender = make_udp_socket();
    cnetmod::socket receiver = make_udp_socket();
    std::optional<cnetmod::endpoint> destination;

    udp_fixture()
    {
        const auto loopback = cnetmod::ip_address::from_string("127.0.0.1");
        if (!loopback)
            throw std::system_error(loopback.error());
        if (auto bound = receiver.bind(cnetmod::endpoint{*loopback, 0U}); !bound)
            throw std::system_error(bound.error());
        if (auto nonblocking = receiver.set_non_blocking(true); !nonblocking)
            throw std::system_error(nonblocking.error());
        auto address = receiver.local_endpoint();
        if (!address)
            throw std::system_error(address.error());
        destination = *address;
    }
};

struct send_owner
{
    std::string payload;
    cnetmod::cancel_token token;

    explicit send_owner(std::string text) : payload(std::move(text)) {}
};

struct send_observation
{
    std::optional<std::expected<std::size_t, std::error_code>> result;
    cnetmod::io_context* resumed_on{};
    bool token_retired{};
    bool done{};
};

auto send_owned(udp_fixture& fixture, std::shared_ptr<send_owner> owner,
    send_observation& observation) -> cnetmod::task<void>
{
    observation.result = co_await cnetmod::async_sendto_on(
        *fixture.socket_context, *fixture.resume_context, fixture.sender,
        cnetmod::const_buffer{owner->payload.data(), owner->payload.size()},
        *fixture.destination, owner->token);
    observation.resumed_on = cnetmod::io_context::current();
    observation.token_retired = !owner->token.pending_.load(std::memory_order_acquire);
    owner.reset();
    observation.done = true;
}

// These polls run only in the test controller, never inside a coroutine. Closing
// on failure still drains the original completion before a frame is destroyed.
auto finish_send(udp_fixture& fixture, send_observation& observation,
    bool poll_socket_context = true) -> bool
{
    const auto pump = [&]
    {
        if (poll_socket_context)
            (void)fixture.socket_context->poll();
        (void)fixture.resume_context->poll();
        std::this_thread::yield();
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!observation.done && std::chrono::steady_clock::now() < deadline)
        pump();
    if (observation.done)
        return true;

    fixture.sender.close();
    const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!observation.done && std::chrono::steady_clock::now() < close_deadline)
        pump();
    // A failed lifetime invariant must not turn test teardown into use-after-free.
    if (!observation.done)
        std::terminate();
    return false;
}

auto receive_payload(udp_fixture& fixture, std::string_view expected) -> bool
{
    std::array<char, 256> bytes{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    do
    {
        const auto received = ::recv(fixture.receiver.native_handle(),
            bytes.data(), static_cast<int>(bytes.size()), 0);
        if (received >= 0)
            return std::string_view{bytes.data(), static_cast<std::size_t>(received)} == expected;
        if (::WSAGetLastError() != WSAEWOULDBLOCK)
            return false;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

} // namespace

TEST(sendto_on_early_cancel_does_not_submit)
{
    cnetmod::net_init network;
    udp_fixture fixture;
    auto owner = std::make_shared<send_owner>("must-not-be-sent");
    owner->token.cancel();
    send_observation observation;
    auto operation = send_owned(fixture, owner, observation);
    fixture.resume_context->post(operation.handle());
    ASSERT_TRUE(finish_send(fixture, observation));
    ASSERT_TRUE(observation.result.has_value());
    if (!observation.result)
        return;
    ASSERT_FALSE(observation.result->has_value());
    if (!observation.result->has_value())
        ASSERT_TRUE(observation.result->error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    ASSERT_TRUE(observation.token_retired);
    ASSERT_TRUE(observation.resumed_on == fixture.resume_context.get());
    std::array<char, 64> bytes{};
    ASSERT_EQ(::recv(fixture.receiver.native_handle(), bytes.data(),
        static_cast<int>(bytes.size()), 0), SOCKET_ERROR);
    ASSERT_EQ(::WSAGetLastError(), WSAEWOULDBLOCK);
}

TEST(sendto_on_cancel_keeps_buffer_and_token_until_original_completion)
{
    cnetmod::net_init network;
    udp_fixture fixture;
    ASSERT_FALSE(fixture.sender.skips_completion_on_success());
    constexpr std::string_view payload{"retained-until-original-cqe"};
    auto owner = std::make_shared<send_owner>(std::string{payload});
    std::weak_ptr<send_owner> lifetime = owner;
    send_observation observation;
    auto operation = send_owned(fixture, owner, observation);
    fixture.resume_context->post(operation.handle());
    // The socket IOCP has no worker yet: even immediate success owes its CQE.
    (void)fixture.resume_context->poll();
    ASSERT_FALSE(observation.done);
    ASSERT_TRUE(owner->token.pending_.load(std::memory_order_acquire));
    owner->token.cancel();
    ASSERT_TRUE(owner->token.pending_.load(std::memory_order_acquire));
    owner.reset();
    ASSERT_FALSE(lifetime.expired());
    (void)fixture.resume_context->poll();
    ASSERT_FALSE(observation.done);
    ASSERT_FALSE(lifetime.expired());

    ASSERT_TRUE(finish_send(fixture, observation));
    ASSERT_TRUE(observation.token_retired);
    ASSERT_TRUE(lifetime.expired());
    ASSERT_TRUE(observation.resumed_on == fixture.resume_context.get());
    ASSERT_TRUE(observation.result.has_value());
    if (!observation.result)
        return;
    if (observation.result->has_value())
    {
        // CancelIoEx may lose to an already accepted datagram. Preserve success.
        ASSERT_EQ(**observation.result, payload.size());
        ASSERT_TRUE(receive_payload(fixture, payload));
    }
    else
        ASSERT_TRUE(observation.result->error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted));
}

TEST(sendto_on_cross_context_completion_and_token_reuse)
{
    cnetmod::net_init network;
    for (const bool skip_success : {false, true})
    {
        udp_fixture fixture;
        if (skip_success)
        {
            const auto configured = fixture.sender.apply_options(
                cnetmod::socket_options{.skip_completion_on_success = true});
            ASSERT_TRUE(configured.has_value());
            if (!configured)
                return;
        }
        auto owner = std::make_shared<send_owner>("cancel-before-reuse");
        if (!skip_success)
        {
            // Reuse the exact token after a cancelled, genuinely submitted I/O,
            // not merely after the no-submission early-cancellation branch.
            // Use a separate destination: cancellation does not promise that a
            // datagram racing completion was never delivered to the peer.
            udp_fixture cancelled_fixture;
            send_observation cancelled;
            auto operation = send_owned(cancelled_fixture, owner, cancelled);
            cancelled_fixture.resume_context->post(operation.handle());
            (void)cancelled_fixture.resume_context->poll();
            ASSERT_FALSE(cancelled.done);
            ASSERT_TRUE(owner->token.pending_.load(std::memory_order_acquire));
            owner->token.cancel();
            ASSERT_TRUE(finish_send(cancelled_fixture, cancelled));
            ASSERT_TRUE(cancelled.token_retired);
            ASSERT_TRUE(cancelled.result.has_value());
            if (!cancelled.result)
                return;
            if (cancelled.result->has_value())
                ASSERT_TRUE(receive_payload(cancelled_fixture, owner->payload));
            else
                ASSERT_TRUE(cancelled.result->error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        }
        std::atomic<bool> stop_worker{false};
        std::jthread socket_worker{[&]
        {
            while (!stop_worker.load(std::memory_order_acquire))
            {
                (void)fixture.socket_context->poll();
                std::this_thread::yield();
            }
        }};
        // The socket worker can deliver a CQE while the origin worker is still
        // returning from WSASendTo. Every iteration reuses the same token.
        for (std::size_t index = 0; index < 256U; ++index)
        {
            owner->token.reset();
            owner->payload = std::format("cross-context-{}", index);
            send_observation observation;
            auto operation = send_owned(fixture, owner, observation);
            fixture.resume_context->post(operation.handle());
            const auto finished = finish_send(fixture, observation, false);
            ASSERT_TRUE(finished);
            ASSERT_TRUE(observation.token_retired);
            ASSERT_TRUE(observation.resumed_on == fixture.resume_context.get());
            ASSERT_TRUE(observation.result.has_value());
            if (!observation.result || !observation.result->has_value())
            {
                ASSERT_TRUE(observation.result && observation.result->has_value());
                break;
            }
            ASSERT_EQ(**observation.result, owner->payload.size());
            ASSERT_TRUE(receive_payload(fixture, owner->payload));
            if (!finished)
                break;
        }
        stop_worker.store(true, std::memory_order_release);
        socket_worker.join();
    }
}
#endif

RUN_TESTS()
