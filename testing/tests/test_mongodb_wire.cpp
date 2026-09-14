#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.protocol.mongodb;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

using namespace cnetmod::mongodb;

TEST(mongodb_op_msg_decodes_more_to_come_response_flag)
{
    bson_document reply{{"ok", bson_value{1.0}}, {"cursor", bson_value{std::int64_t{0}}}};
    auto wire = encode_command_message(17, reply, 1024U * 1024U);
    ASSERT_TRUE(wire.has_value());
    // OP_MSG flags immediately follow the 16-byte message header. A response
    // with moreToCome has the same body as a one-shot reply, but clients must
    // keep reading it through command_stream rather than reuse the socket.
    ASSERT_TRUE(wire->size() > 20U);
    (*wire)[16] = std::byte{op_message_more_to_come};
    (*wire)[17] = std::byte{0};
    (*wire)[18] = std::byte{0};
    (*wire)[19] = std::byte{0};

    auto decoded = decode_command_message(*wire, 1024U * 1024U);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE((decoded->flags & op_message_more_to_come) != 0U);
    ASSERT_TRUE(decoded->body.contains("ok"));
}

TEST(mongodb_op_msg_rejects_more_to_come_as_a_request_flag)
{
    bson_document command{{"find", bson_value{"users"}}};
    auto encoded = encode_command_message(1, command, 1024U * 1024U,
        op_message_more_to_come);
    ASSERT_FALSE(encoded.has_value());
}

TEST(mongodb_op_msg_allows_exhaust_capability_on_requests)
{
    bson_document command{{"find", bson_value{"users"}}};
    auto encoded = encode_command_message(1, command, 1024U * 1024U,
        op_message_exhaust_allowed);
    ASSERT_TRUE(encoded.has_value());
}

TEST(mongodb_maintenance_stop_wakes_a_long_interval)
{
    for (bool stop_before_start : {false, true})
    {
        auto io = cnetmod::make_io_context();
        connection_pool_options options;
        options.minimum_size = 0;
        options.health_check_interval = std::chrono::hours{1};
        connection_pool pool{*io, options};
        std::stop_source stop;
        if (stop_before_start)
            stop.request_stop();
        bool finished = false;
        auto maintenance = [&]() -> cnetmod::task<void>
        {
            co_await pool.run_maintenance(stop.get_token());
            finished = true;
            io->stop();
        };
        auto cancel = [&]() -> cnetmod::task<void>
        {
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
            stop.request_stop();
        };
        cnetmod::spawn(*io, maintenance());
        if (!stop_before_start)
            cnetmod::spawn(*io, cancel());
        const auto started = std::chrono::steady_clock::now();
        io->run();
        ASSERT_TRUE(finished);
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
    }
}

TEST(mongodb_awaitable_close_is_idempotent_and_retains_state)
{
    auto io = cnetmod::make_io_context();
    auto pool = std::make_unique<connection_pool>(*io, connection_pool_options{});
    auto first = pool->async_close();
    auto second = pool->async_close();
    first.handle().resume();
    ASSERT_TRUE(first.handle().done());
    first.handle().promise().result();
    auto acquire = pool->acquire();
    acquire.handle().resume();
    ASSERT_TRUE(acquire.handle().done());
    const auto result = acquire.handle().promise().result();
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().code == error_code::connection_closed);
    pool.reset();
    second.handle().resume();
    ASSERT_TRUE(second.handle().done());
    second.handle().promise().result();
}

TEST(mongodb_empty_health_cancellation_releases_creation_for_retry)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    connection_pool_options options;
    options.connection.port = listener->local_endpoint()->port();
    options.connection.command_timeout = std::chrono::milliseconds{0};
    options.minimum_size = 0;
    options.maximum_size = 1;
    connection_pool pool{*io, options};
    auto settle = [&](auto& operation)
    {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        if (!operation.handle().done())
            std::terminate();
    };
    for (unsigned attempt = 0; attempt < 2; ++attempt)
    {
        auto accepting = cnetmod::async_accept(*io, *listener);
        accepting.handle().resume();
        cnetmod::cancel_token cancellation;
        auto checking = pool.health_check(cancellation);
        checking.handle().resume();
        settle(accepting);
        auto peer = accepting.handle().promise().result();
        ASSERT_TRUE(peer.has_value());
        std::array<std::byte, 1> first;
        auto receiving = cnetmod::async_read(*io, *peer,
            cnetmod::mutable_buffer{first.data(), first.size()});
        receiving.handle().resume();
        settle(receiving);
        ASSERT_TRUE(receiving.handle().promise().result().has_value());
        ASSERT_FALSE(checking.handle().done());
        ASSERT_EQ(pool.connecting_count(), 1U);
        std::jthread cancelling{[&]()
            {
                cancellation.cancel();
            }};
        cancelling.join();
        settle(checking);
        auto result = checking.handle().promise().result();
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error().code == error_code::operation_cancelled);
        ASSERT_FALSE(cancellation.pending_.load());
        ASSERT_EQ(pool.connecting_count(), 0U);
        ASSERT_EQ(pool.checked_out_count(), 0U);
        ASSERT_EQ(pool.size(), 0U);
        auto disconnected = [&]() -> cnetmod::task<bool>
        {
            std::array<std::byte, 4096> bytes;
            std::size_t total = 0;
            while (total <= 65536)
            {
                auto read = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                if (!read)
                    co_return read.error() == cnetmod::make_error_code(cnetmod::errc::end_of_file) ||
                        read.error() == std::errc::connection_reset ||
                        read.error() == std::errc::connection_aborted;
                if (*read == 0)
                    co_return true;
                total += *read;
            }
            co_return false;
        };
        auto observing = disconnected();
        observing.handle().resume();
        settle(observing);
        ASSERT_TRUE(observing.handle().promise().result());
    }
    auto closing = pool.async_close();
    closing.handle().resume();
    settle(closing);
    closing.handle().promise().result();
}

TEST(mongodb_pool_close_settles_all_concurrent_connection_attempts)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    for (bool deferred_close : {false, true})
    {
        connection_pool_options options;
        options.connection.port = listener->local_endpoint()->port();
        options.connection.command_timeout = std::chrono::milliseconds{0};
        options.connection.connect_timeout = std::chrono::seconds{30};
        options.minimum_size = 3;
        options.maximum_size = 3;
        options.maximum_connecting = 3;
        connection_pool pool{*io, options};
        std::vector<cnetmod::task<result<pooled_connection>>> attempts;
        attempts.reserve(3);
        for (unsigned index = 0; index < 3; ++index)
        {
            attempts.push_back(pool.acquire());
            attempts.back().handle().resume();
        }
        ASSERT_EQ(pool.connecting_count(), 3U);
        (void)io->poll();
        cnetmod::cancel_token cancellation;
        auto cancelled_warmup = pool.warm_up(cancellation);
        cancelled_warmup.handle().resume();
        ASSERT_FALSE(cancelled_warmup.handle().done());
        std::jthread requester{[&]()
            {
                cancellation.cancel();
            }};
        requester.join();
        const auto cancellation_limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!cancelled_warmup.handle().done() && std::chrono::steady_clock::now() < cancellation_limit)
            (void)io->poll();
        if (!cancelled_warmup.handle().done())
            std::terminate();
        auto cancelled = cancelled_warmup.handle().promise().result();
        ASSERT_FALSE(cancelled.has_value());
        ASSERT_TRUE(cancelled.error().code == error_code::operation_cancelled);
        ASSERT_FALSE(cancellation.pending_.load());
        ASSERT_EQ(pool.connecting_count(), 3U);
        for (auto& attempt : attempts)
            ASSERT_FALSE(attempt.handle().done());
        auto warming = pool.warm_up();
        warming.handle().resume();
        ASSERT_FALSE(warming.handle().done());
        if (!deferred_close)
            pool.close();
        auto closing = pool.async_close();
        closing.handle().resume();
        const auto finished = [&]()
        {
            return closing.handle().done() && warming.handle().done() &&
                std::ranges::all_of(attempts, [](auto& attempt)
                    {
                        return attempt.handle().done();
                    });
        };
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!finished() && std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        if (!finished())
            std::terminate();
        closing.handle().promise().result();
        auto warmed = warming.handle().promise().result();
        ASSERT_FALSE(warmed.has_value());
        ASSERT_TRUE(warmed.error().code == error_code::connection_closed);
        for (auto& attempt : attempts)
        {
            auto outcome = attempt.handle().promise().result();
            ASSERT_FALSE(outcome.has_value());
            ASSERT_TRUE(outcome.error().code == error_code::connection_closed);
        }
        ASSERT_EQ(pool.connecting_count(), 0U);
        ASSERT_EQ(pool.checked_out_count(), 0U);
        ASSERT_EQ(pool.waiter_count(), 0U);
        ASSERT_EQ(pool.size(), 0U);
        pool.close();
        (void)io->poll();
    }
}

TEST(mongodb_connect_early_cancellation_releases_transport_and_token)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    connection client{*io};
    connection_options options;
    options.port = listener->local_endpoint()->port();
    options.command_timeout = std::chrono::milliseconds{0};
    for (bool precancelled : {true, false})
    {
        cnetmod::cancel_token cancellation;
        if (precancelled)
            cancellation.cancel();
        auto connecting = client.connect(options, cancellation);
        connecting.handle().resume();
        if (precancelled)
            ASSERT_TRUE(connecting.handle().done());
        else
        {
            ASSERT_FALSE(connecting.handle().done());
            std::jthread requester{[&]()
                {
                    cancellation.cancel();
                }};
            requester.join();
        }
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!connecting.handle().done() && std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        if (!connecting.handle().done())
            std::terminate();
        auto outcome = connecting.handle().promise().result();
        ASSERT_FALSE(outcome.has_value());
        ASSERT_TRUE(outcome.error().code == error_code::operation_cancelled);
        ASSERT_FALSE(client.is_open());
        ASSERT_FALSE(cancellation.pending_.load());
        cancellation.reset();
        cancellation.cancel();
        (void)io->poll();
        ASSERT_FALSE(cancellation.pending_.load());
    }
}

TEST(mongodb_active_hello_cancel_settles_without_waiting_for_deadline)
{
    for (auto timeout : {std::chrono::milliseconds{0}, std::chrono::milliseconds{30000}})
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        connection client{*io};
        connection_options options;
        options.port = listener->local_endpoint()->port();
        options.connect_timeout = std::chrono::seconds{1};
        options.command_timeout = timeout;
        for (unsigned attempt = 0; attempt < 2; ++attempt)
        {
            cnetmod::cancel_token cancellation;
            auto connecting = attempt == 0 ? client.connect(options) : client.connect(options, cancellation);
            connecting.handle().resume();
            const auto admission_limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (!client.is_open() && !connecting.handle().done() &&
                std::chrono::steady_clock::now() < admission_limit)
                (void)io->poll();
            ASSERT_TRUE(client.is_open());
            ASSERT_FALSE(connecting.handle().done());
            if (!client.is_open() || connecting.handle().done())
                std::terminate();
            auto overlapping = client.connect(options);
            overlapping.handle().resume();
            ASSERT_TRUE(overlapping.handle().done());
            auto rejected = overlapping.handle().promise().result();
            ASSERT_FALSE(rejected.has_value());
            ASSERT_TRUE(rejected.error().code == error_code::protocol_error);
            ASSERT_TRUE(client.is_open());
            ASSERT_FALSE(connecting.handle().done());
            const auto cancellation_started = std::chrono::steady_clock::now();
            std::jthread requester{[&]()
                {
                    if (attempt == 0)
                        client.cancel_active_command();
                    else
                        cancellation.cancel();
                }};
            requester.join();
            while (!connecting.handle().done() &&
                std::chrono::steady_clock::now() - cancellation_started < std::chrono::seconds{2})
                (void)io->poll();
            if (!connecting.handle().done())
                std::terminate();
            auto result = connecting.handle().promise().result();
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error().code == error_code::operation_cancelled);
            ASSERT_FALSE(client.is_open());
            ASSERT_FALSE(cancellation.pending_.load());
            (void)io->poll();
        }
    }
}

TEST(mongodb_warmup_rejects_requested_and_completed_close)
{
    for (bool complete_close : {false, true})
    {
        auto io = cnetmod::make_io_context();
        connection_pool pool{*io, connection_pool_options{}};
        auto closing = pool.async_close();
        if (complete_close)
        {
            closing.handle().resume();
            ASSERT_TRUE(closing.handle().done());
            closing.handle().promise().result();
        }
        auto warming = pool.warm_up();
        warming.handle().resume();
        ASSERT_TRUE(warming.handle().done());
        auto result = warming.handle().promise().result();
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error().code == error_code::connection_closed);
        ASSERT_EQ(pool.size(), 0U);
        if (!complete_close)
        {
            closing.handle().resume();
            ASSERT_TRUE(closing.handle().done());
            closing.handle().promise().result();
        }
    }
}

RUN_TESTS()
