#include "test_framework.hpp"

import std;
import cnetmod.protocol.redis;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.instrumentation.tracing;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

using namespace cnetmod::redis;

TEST(redis_dynamic_request_rejects_empty_and_counts_commands)
{
    request batch;
    const std::vector<std::string> empty;
    ASSERT_FALSE(batch.push(empty));
    ASSERT_TRUE(batch.empty());

    const std::vector<std::string> command{"SET", "key", "value"};
    ASSERT_TRUE(batch.push(command));
    ASSERT_EQ(batch.size(), 1U);
    ASSERT_TRUE(batch.payload().starts_with("*3\r\n$3\r\nSET\r\n"));
}

TEST(redis_cluster_rejects_nonzero_database_before_network_io)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cluster_client cluster{*io};
    connect_options options;
    options.db = 1;
    cnetmod::cancel_token cancellation;
    auto result = cnetmod::sync_wait(cluster.connect(options, cancellation));
    ASSERT_FALSE(result.has_value());
    if (!result)
        ASSERT_EQ(result.error(),
            std::make_error_code(std::errc::invalid_argument));
}

TEST(redis_cluster_hash_tags_support_safe_multi_key_commands)
{
    const auto first = make_cluster_key("sessions", "tenant-42", "primary");
    const auto second = make_cluster_key("sessions", "tenant-42", "backup");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    if (!first || !second)
        return;
    const std::array<std::string_view, 2> colocated{*first, *second};
    ASSERT_TRUE(keys_share_slot(colocated));
    ASSERT_EQ(client::key_slot(*first), client::key_slot(*second));

    const std::array<std::string_view, 2> split{
        "sessions:{tenant-1}:primary", "sessions:{tenant-2}:backup"};
    ASSERT_FALSE(keys_share_slot(split));
    ASSERT_FALSE(make_cluster_key("sessions", "bad{tag", "key").has_value());
}

TEST(redis_pool_stop_joins_a_stalled_authentication_task)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    connection_pool pool{*io, {.host = "127.0.0.1", .port = endpoint->port(), .password = "test-password", .resp3 = false, .initial_size = 1, .max_size = 1, .connect_timeout = std::chrono::hours{1}}};
    bool command_seen = false, peer_closed = false, run_finished = false, joined = false;
    auto server = [&]() -> cnetmod::task<void>
    {
        auto peer = co_await cnetmod::async_accept(*io, *listener);
        if (!peer)
        {
            io->stop();
            co_return;
        }
        std::array<char, 128> bytes{};
        while (true)
        {
            auto received = co_await cnetmod::async_read(*io, *peer,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            if (!received || *received == 0U)
                break;
            command_seen = true;
        }
        peer_closed = true;
    };
    auto runner = [&]() -> cnetmod::task<void>
    {
        co_await pool.async_run();
        run_finished = true;
    };
    auto shutdown = [&]() -> cnetmod::task<void>
    {
        while (!command_seen)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        co_await pool.cancel();
        joined = pool.pending_maintenance() == 0;
        while (!run_finished || !peer_closed)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };
    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, runner());
    cnetmod::spawn(*io, shutdown());
    io->run();
    ASSERT_TRUE(joined);
    ASSERT_TRUE(run_finished);
    ASSERT_TRUE(peer_closed);
}

TEST(redis_connect_deadline_cancels_hello_auth_and_select)
{
    for (int phase = 0; phase < 3; ++phase)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        request expected;
        if (phase == 0)
            expected.push("HELLO", "3");
        else if (phase == 1)
            expected.push("AUTH", "test-password");
        else
            expected.push("SELECT", "1");
        bool cancelled = false, peer_closed = false;
        unsigned finished = 0;
        auto finish = [&]
        {
            if (++finished == 2U)
                io->stop();
        };
        auto broker = [&]() -> cnetmod::task<void>
        {
            auto peer = co_await cnetmod::async_accept(*io, *listener);
            if (!peer)
            {
                io->stop();
                co_return;
            }
            std::string bytes;
            std::array<char, 128> buffer{};
            while (true)
            {
                auto received = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{buffer.data(), buffer.size()});
                if (!received || *received == 0U)
                    break;
                bytes.append(buffer.data(), *received);
            }
            peer_closed = bytes == expected.payload();
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            client connection{*io};
            connect_options options{.host = "127.0.0.1", .port = endpoint->port()};
            options.resp3 = phase == 0;
            if (phase == 1)
                options.password = "test-password";
            if (phase == 2)
                options.db = 1;
            cnetmod::cancel_token token;
            auto result = co_await cnetmod::with_timeout(*io, std::chrono::milliseconds{150},
                connection.connect(options, token), token);
            cancelled = !result && result.error() == std::errc::timed_out && !connection.is_open();
            finish();
        };
        cnetmod::spawn(*io, broker());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(cancelled);
        ASSERT_TRUE(peer_closed);
    }
}

TEST(redis_cancellable_exchange_handles_fragments_errors_and_total_budget)
{
    for (int mode = 0; mode < 5; ++mode)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                       cnetmod::ipv4_address::loopback(), 0})
                .has_value());
        ASSERT_TRUE(listener->listen().has_value());
        auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        request batch;
        batch.push("HELLO", "3");
        if (mode == 4)
            batch.push("PING");
        bool correct = false;
        unsigned finished = 0;
        auto finish = [&]
        {
            if (++finished == 2U)
                io->stop();
        };
        auto broker = [&]() -> cnetmod::task<void>
        {
            auto peer = co_await cnetmod::async_accept(*io, *listener);
            if (!peer)
            {
                io->stop();
                co_return;
            }
            std::string bytes(batch.payload().size(), '\0');
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                auto read = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{bytes.data() + offset, bytes.size() - offset});
                if (!read || *read == 0U)
                    break;
                offset += *read;
            }
            if (mode != 3)
            {
                const std::string_view reply = mode == 0 ? "%1\r\n+server\r\n$5\r\nredis\r\n"
                    : mode == 1                          ? "-DENIED\r\n"
                    : mode == 2                          ? "?invalid\r\n"
                                                         : "+OK\r\n+PONG\r\n";
                for (const char byte : reply)
                {
                    auto sent = co_await cnetmod::async_write_all(*io, *peer,
                        cnetmod::const_buffer{&byte, 1});
                    if (!sent)
                        break;
                }
            }
            else
            {
                char byte{};
                (void)co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{&byte, 1});
            }
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            client connection{*io};
            auto connected = co_await connection.connect({.host = "127.0.0.1",
                .port = endpoint->port(),
                .resp3 = false});
            if (!connected)
            {
                io->stop();
                co_return;
            }
            cnetmod::cancel_token token;
            auto result = co_await cnetmod::with_timeout(*io, std::chrono::milliseconds{150},
                connection.exchange(batch, token, mode == 4 ? 10 : 65536), token);
            if (mode == 0)
                correct = result && result->size() == 3U && result->back().value == "redis";
            else if (mode == 1)
                correct = result && has_error(*result) && connection.is_open();
            else
                correct = !result && !connection.is_open() && result.error() == (mode == 2 ? make_error_code(redis_errc::invalid_data_type) : std::make_error_code(mode == 3 ? std::errc::timed_out : std::errc::message_size));
            connection.close();
            finish();
        };
        cnetmod::spawn(*io, broker());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(correct);
    }
}

TEST(redis_failed_authentication_closes_the_uncommitted_session)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                   cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    bool rejected = false;
    bool peer_closed = false;
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2U)
            io->stop();
    };
    auto broker = [&]() -> cnetmod::task<void>
    {
        auto peer = co_await cnetmod::async_accept(*io, *listener);
        if (!peer)
        {
            io->stop();
            co_return;
        }
        request expected;
        expected.push("AUTH", "test-only-password");
        std::string bytes(expected.payload().size(), '\0');
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            auto read = co_await cnetmod::async_read(*io, *peer,
                cnetmod::mutable_buffer{bytes.data() + offset, bytes.size() - offset});
            if (!read || *read == 0U)
                break;
            offset += *read;
        }
        constexpr std::string_view reply = "-WRONGPASS authentication rejected\r\n";
        (void)co_await cnetmod::async_write_all(*io, *peer,
            cnetmod::const_buffer{reply.data(), reply.size()});
        char byte{};
        auto read = co_await cnetmod::async_read(*io, *peer,
            cnetmod::mutable_buffer{&byte, 1});
        peer_closed = (!read || *read == 0U) && bytes == expected.payload();
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        client connection{*io};
        auto result = co_await connection.connect({.host = "127.0.0.1",
            .port = endpoint->port(),
            .password = "test-only-password",
            .resp3 = false});
        rejected = !result && !connection.is_open() && !connection.is_resp3();
        finish();
    };
    cnetmod::spawn(*io, broker());
    cnetmod::spawn(*io, exercise());
    io->run();
    ASSERT_TRUE(rejected);
    ASSERT_TRUE(peer_closed);
}

TEST(redis_reconnect_does_not_reuse_buffered_replies_from_the_previous_session)
{
    for (const bool explicit_close : {false, true})
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                       cnetmod::ipv4_address::loopback(), 0})
                .has_value());
        ASSERT_TRUE(listener->listen().has_value());
        auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        bool correct = false;
        unsigned finished = 0;
        auto finish = [&]
        {
            if (++finished == 2U)
                io->stop();
        };
        auto broker = [&]() -> cnetmod::task<void>
        {
            for (int session = 0; session < 2; ++session)
            {
                auto peer = co_await cnetmod::async_accept(*io, *listener);
                if (!peer)
                {
                    io->stop();
                    co_return;
                }
                // Send before reading to make the old session's surplus reply
                // available to the client's buffered parser in one read.
                const std::string_view reply = session == 0
                    ? "+OLD\r\n+STALE\r\n"
                    : "+FRESH\r\n";
                (void)co_await cnetmod::async_write_all(*io, *peer,
                    cnetmod::const_buffer{reply.data(), reply.size()});
                std::array<char, 128> bytes{};
                (void)co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            }
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            client connection{*io};
            connect_options options{.host = "127.0.0.1",
                .port = endpoint->port(),
                .resp3 = false};
            auto first = co_await connection.connect(options);
            if (!first)
            {
                io->stop();
                co_return;
            }
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{10});
            auto old = co_await connection.cmd({"GET", "key"});
            if (explicit_close)
                connection.close();
            auto second = co_await connection.connect(options);
            auto fresh = co_await connection.cmd({"GET", "key"});
            correct = old && second && fresh && first_value(*old) == "OLD" &&
                first_value(*fresh) == "FRESH";
            connection.close();
            finish();
        };
        cnetmod::spawn(*io, broker());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(correct);
    }
}

TEST(redis_pool_shutdown_drains_waiters_and_rejects_new_borrowers)
{
    auto io = cnetmod::make_io_context();
    connection_pool pool{*io, {.initial_size = 0, .max_size = 0, .ping_interval = std::chrono::hours{1}}};
    std::array<cnetmod::cancel_token, 8> tokens;
    std::array<std::error_code, 8> outcomes;
    unsigned finished = 0;
    bool run_finished = false;
    bool rejected = false;
    auto runner = [&]() -> cnetmod::task<void>
    {
        co_await pool.async_run();
        run_finished = true;
    };
    auto borrow = [&](std::size_t index) -> cnetmod::task<void>
    {
        auto result = co_await pool.async_get_connection(tokens[index]);
        if (!result)
            outcomes[index] = result.error();
        ++finished;
    };
    auto stop = [&]() -> cnetmod::task<void>
    {
        while (pool.waiter_count() < tokens.size())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        tokens[0].cancel_due_to_deadline();
        tokens[1].cancel();
        co_await pool.cancel();
        co_await pool.cancel();
        cnetmod::cancel_token fresh;
        auto late = co_await pool.async_get_connection(fresh);
        auto immediate = pool.try_get_connection();
        rejected = !late && !immediate && pool.waiter_count() == 0U &&
            late.error() == std::errc::operation_canceled &&
            immediate.error() == std::errc::operation_canceled;
        while (finished != tokens.size() || !run_finished)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };
    cnetmod::spawn(*io, runner());
    for (std::size_t index = 0; index < tokens.size(); ++index)
        cnetmod::spawn(*io, borrow(index));
    cnetmod::spawn(*io, stop());
    io->run();
    ASSERT_TRUE(rejected);
    ASSERT_TRUE(outcomes[0] == std::errc::timed_out);
    for (std::size_t index = 1; index < outcomes.size(); ++index)
        ASSERT_TRUE(outcomes[index] == std::errc::operation_canceled);
}

TEST(redis_pool_never_reissues_a_closed_returned_lease)
{
    for (const bool queued_waiter : {false, true})
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                       cnetmod::ipv4_address::loopback(), 0})
                .has_value());
        ASSERT_TRUE(listener->listen().has_value());
        auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        connection_pool pool{*io, {.host = "127.0.0.1", .port = endpoint->port(), .resp3 = false, .initial_size = 1, .max_size = 1, .ping_interval = std::chrono::hours{1}}};
        cnetmod::cancel_token waiter_token;
        bool rejected = false;
        bool waiter_finished = false;
        bool waiter_rejected = false;
        bool run_finished = false;
        bool peer_finished = false;
        bool recovered = false;
        auto broker = [&]() -> cnetmod::task<void>
        {
            auto peer = co_await cnetmod::async_accept(*io, *listener);
            if (peer)
            {
                char byte{};
                (void)co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{&byte, 1});
            }
            auto replacement = co_await cnetmod::async_accept(*io, *listener);
            if (replacement)
            {
                constexpr std::string_view command = "*1\r\n$4\r\nPING\r\n";
                std::array<char, command.size()> bytes{};
                std::size_t offset = 0;
                while (offset < bytes.size())
                {
                    auto received = co_await cnetmod::async_read(*io, *replacement,
                        cnetmod::mutable_buffer{bytes.data() + offset, bytes.size() - offset});
                    if (!received || *received == 0U)
                        break;
                    offset += *received;
                }
                if (std::string_view{bytes.data(), offset} == command)
                {
                    constexpr std::string_view reply = "+PONG\r\n";
                    (void)co_await cnetmod::async_write_all(*io, *replacement,
                        cnetmod::const_buffer{reply.data(), reply.size()});
                }
            }
            peer_finished = true;
        };
        auto runner = [&]() -> cnetmod::task<void>
        {
            co_await pool.async_run();
            run_finished = true;
        };
        auto waiter = [&]() -> cnetmod::task<void>
        {
            auto result = co_await pool.async_get_connection(waiter_token);
            waiter_rejected = !result;
            waiter_finished = true;
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            auto lease = co_await pool.async_get_connection();
            if (!lease)
            {
                io->stop();
                co_return;
            }
            auto* original = &lease->get();
            *lease = pooled_connection{};
            auto reused = pool.try_get_connection();
            if (!reused || &reused->get() != original || !reused->get().is_open())
            {
                io->stop();
                co_return;
            }
            *lease = std::move(*reused);
            if (queued_waiter)
            {
                cnetmod::spawn(*io, waiter());
                while (pool.waiter_count() == 0U && !waiter_finished)
                    co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
            }
            lease->get().close();
            *lease = pooled_connection{};
            rejected = pool.idle_count() == 0U && !pool.try_get_connection();
            waiter_token.cancel();
            auto replacement = co_await pool.async_get_connection(
                cnetmod::deadline::after(std::chrono::seconds{1}));
            if (replacement)
            {
                cnetmod::cancel_token ping_token;
                auto pong = co_await cnetmod::with_timeout(*io, std::chrono::seconds{1},
                    replacement->get().ping(ping_token), ping_token);
                recovered = pong.has_value();
                replacement->get().close();
                *replacement = pooled_connection{};
            }
            co_await pool.cancel();
            rejected = rejected && pool.pending_maintenance() == 0;
            // Shutdown must wake maintenance instead of waiting for the heartbeat interval.
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{100});
            if (queued_waiter)
                rejected = rejected && waiter_finished && waiter_rejected;
            rejected = rejected && run_finished && peer_finished;
            io->stop();
        };
        cnetmod::spawn(*io, broker());
        cnetmod::spawn(*io, runner());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(rejected);
        ASSERT_TRUE(recovered);
    }
}

TEST(redis_health_ping_is_cancellable_and_invalidates_incomplete_exchanges)
{
    for (int mode = 0; mode < 4; ++mode)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                       cnetmod::ipv4_address::loopback(), 0})
                .has_value());
        ASSERT_TRUE(listener->listen().has_value());
        const auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        bool wire_matches = false;
        bool outcome_matches = false;
        bool ownership_matches = false;
        unsigned completed = 0;
        auto finish = [&]
        {
            if (++completed == 2U)
                io->stop();
        };
        auto broker = [&]() -> cnetmod::task<void>
        {
            auto peer = co_await cnetmod::async_accept(*io, *listener);
            if (!peer)
            {
                io->stop();
                co_return;
            }
            constexpr std::string_view expected = "*1\r\n$4\r\nPING\r\n";
            std::array<char, expected.size()> request_bytes{};
            std::size_t offset = 0;
            while (offset < request_bytes.size())
            {
                auto received = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{request_bytes.data() + offset, request_bytes.size() - offset});
                if (!received || *received == 0U)
                    break;
                offset += *received;
            }
            wire_matches = mode == 3 ? offset == 0U
                                     : std::string_view{request_bytes.data(), offset} == expected;
            if (mode < 2)
            {
                const std::string_view reply = mode == 0 ? "+PONG\r\n" : "-DENIED\r\n";
                // Separate writes also cover incremental prefix validation.
                for (const char byte : reply)
                {
                    auto sent = co_await cnetmod::async_write_all(*io, *peer,
                        cnetmod::const_buffer{&byte, 1});
                    if (!sent)
                        break;
                }
            }
            // A silent peer must observe EOF after the client's deadline fires.
            if (mode == 2)
            {
                char byte{};
                auto received = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{&byte, 1});
                wire_matches = wire_matches && (!received || *received == 0U);
            }
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            client connection{*io};
            auto connected = co_await connection.connect({.host = "127.0.0.1", .port = endpoint->port(), .resp3 = false});
            if (!connected)
            {
                io->stop();
                co_return;
            }
            cnetmod::cancel_token token;
            if (mode == 3)
                token.cancel();
            auto result = co_await cnetmod::with_timeout(*io,
                std::chrono::milliseconds{100}, connection.ping(token), token);
            outcome_matches = mode == 0 ? result.has_value()
                                        : !result && result.error() == std::make_error_code(mode == 1 ? std::errc::protocol_error : mode == 2 ? std::errc::timed_out
                                                                                                                                              : std::errc::operation_canceled);
            ownership_matches = connection.is_open() == (mode == 0 || mode == 3);
            connection.close();
            finish();
        };
        cnetmod::spawn(*io, broker());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(wire_matches);
        ASSERT_TRUE(outcome_matches);
        ASSERT_TRUE(ownership_matches);
    }
}

TEST(redis_wire_error_observation_preserves_response_and_connection)
{
    for (int mode = 0; mode < 4; ++mode)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                       cnetmod::ipv4_address::loopback(), 0})
                .has_value());
        ASSERT_TRUE(listener->listen().has_value());
        const auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        request expected;
        expected.push("GET", "private-key");
        if (mode != 0)
            expected.push("GET", "private-key");
        std::vector<std::string> wire_requests;
        std::vector<cnetmod::instrumentation::completed_span> spans;
        bool same_error = false;
        bool connection_usable = false;
        unsigned completed = 0;
        auto finish = [&]
        {
            if (++completed == 2U)
                io->stop();
        };
        auto broker = [&]() -> cnetmod::task<void>
        {
            auto peer = co_await cnetmod::async_accept(*io, *listener);
            if (!peer)
            {
                io->stop();
                co_return;
            }
            for (unsigned index = 0; index < 3U; ++index)
            {
                std::string received(expected.payload().size(), '\0');
                std::size_t offset = 0;
                while (offset < received.size())
                {
                    auto read = co_await cnetmod::async_read(*io, *peer,
                        cnetmod::mutable_buffer{received.data() + offset, received.size() - offset});
                    if (!read || *read == 0U)
                    {
                        io->stop();
                        co_return;
                    }
                    offset += *read;
                }
                wire_requests.push_back(std::move(received));
                const std::string reply = std::string{index < 2U
                                                  ? "-WRONGTYPE private-server-detail\r\n"
                                                  : "+OK\r\n"} +
                    (mode != 0 ? "+OK\r\n" : "");
                if (!(co_await cnetmod::async_write_all(*io, *peer,
                        cnetmod::const_buffer{reply.data(), reply.size()})))
                {
                    io->stop();
                    co_return;
                }
            }
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            client connection{*io};
            auto connected = co_await connection.connect({.host = "127.0.0.1", .port = endpoint->port(), .resp3 = false});
            if (!connected)
            {
                io->stop();
                co_return;
            }
            const auto parent = cnetmod::instrumentation::new_root_context();
            const std::vector<std::string> arguments{"GET", "private-key"};
            auto sink = [&](const cnetmod::instrumentation::completed_span& span)
            {
                spans.push_back(span);
                throw std::runtime_error("collector unavailable");
            };
            const std::vector<std::vector<std::string>> batches{arguments, arguments};
            const std::initializer_list<std::initializer_list<std::string_view>> lists{
                {"GET", "private-key"}, {"GET", "private-key"}};
            auto execute = [&](cnetmod::instrumentation::span_exporter exporter)
                -> cnetmod::task<std::expected<std::vector<resp3_node>, std::string>>
            {
                if (mode == 1)
                    return connection.exec(expected, parent, std::move(exporter));
                if (mode == 2)
                    return connection.pipe(std::span<const std::vector<std::string>>{batches},
                        parent, std::move(exporter));
                if (mode == 3)
                    return connection.pipe(lists, parent, std::move(exporter));
                return connection.cmd(std::span<const std::string>{arguments},
                    parent, std::move(exporter));
            };
            auto plain = co_await execute({});
            auto observed = co_await execute(sink);
            same_error = plain && observed && has_error(*plain) && has_error(*observed) &&
                error_message(*plain) == error_message(*observed);
            auto next = co_await execute(sink);
            connection_usable = next && is_ok(*next);
            connection.close();
            finish();
        };
        cnetmod::spawn(*io, broker());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_EQ(completed, 2U);
        ASSERT_TRUE(same_error);
        ASSERT_TRUE(connection_usable);
        ASSERT_EQ(spans.size(), std::size_t{2});
        ASSERT_TRUE(spans[0].failed);
        ASSERT_FALSE(spans[1].failed);
        ASSERT_EQ(wire_requests.size(), std::size_t{3});
        for (const auto& bytes : wire_requests)
            ASSERT_EQ(bytes, expected.payload());
        for (const auto& span : spans)
            for (const auto& attribute : span.attributes)
                ASSERT_FALSE(attribute.second.contains("private-"));
    }
}

TEST(redis_observation_preserves_empty_command_failure_and_parent)
{
    auto io = cnetmod::make_io_context();
    client connection{*io};
    std::vector<std::string> arguments;
    auto parent = cnetmod::instrumentation::new_root_context();
    unsigned completions = 0;
    bool failed = false;
    std::string parent_id;
    auto sink = [&](const cnetmod::instrumentation::completed_span& span)
    {
        ++completions;
        failed = span.failed;
        parent_id = span.parent_span_id;
        throw std::runtime_error("export failure");
    };
    auto observed = cnetmod::sync_wait(connection.cmd(
        std::span<const std::string>{arguments}, parent, sink));
    auto plain = cnetmod::sync_wait(connection.cmd(
        std::span<const std::string>{arguments}, parent, {}));
    ASSERT_FALSE(observed.has_value());
    ASSERT_FALSE(plain.has_value());
    ASSERT_EQ(observed.error(), plain.error());
    ASSERT_EQ(observed.error(), "empty command");
    ASSERT_EQ(completions, 1U);
    ASSERT_TRUE(failed);
    ASSERT_EQ(parent_id, parent.span_id);
}

TEST(redis_cluster_key_slot_uses_hash_tag)
{
    auto a = client::key_slot("user:{42}:name");
    auto b = client::key_slot("cart:{42}:items");
    auto c = client::key_slot("user:42:name");

    ASSERT_EQ(a, b);
    ASSERT_NE(a, c);
    ASSERT_EQ(client::key_slot("123456789"), 12739);
}

TEST(redis_parse_moved_redirect)
{
    std::vector<resp3_node> nodes{
        resp3_node{
            .data_type = resp3_type::simple_error,
            .value = "MOVED 3999 127.0.0.1:7001",
        },
    };

    auto r = client::parse_redirect(nodes);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(static_cast<int>(r->kind), static_cast<int>(redirect_kind::moved));
    ASSERT_EQ(r->slot, 3999);
    ASSERT_EQ(r->endpoint.host, std::string("127.0.0.1"));
    ASSERT_EQ(r->endpoint.port, 7001);
}

TEST(redis_parse_ask_redirect)
{
    std::vector<resp3_node> nodes{
        resp3_node{
            .data_type = resp3_type::simple_error,
            .value = "ASK 1234 redis-node.local:6380",
        },
    };

    auto r = client::parse_redirect(nodes);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(static_cast<int>(r->kind), static_cast<int>(redirect_kind::ask));
    ASSERT_EQ(r->slot, 1234);
    ASSERT_EQ(r->endpoint.host, std::string("redis-node.local"));
    ASSERT_EQ(r->endpoint.port, 6380);
}

TEST(redis_parse_moved_redirect_ipv6_endpoint)
{
    std::vector<resp3_node> nodes{
        resp3_node{
            .data_type = resp3_type::simple_error,
            .value = "MOVED 42 [2001:db8::10]:7002",
        },
    };

    auto r = client::parse_redirect(nodes);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(static_cast<int>(r->kind), static_cast<int>(redirect_kind::moved));
    ASSERT_EQ(r->slot, 42);
    ASSERT_EQ(r->endpoint.host, std::string("2001:db8::10"));
    ASSERT_EQ(r->endpoint.port, 7002);
}

TEST(redis_resp3_push_parse)
{
    auto parsed = parse_response(">3\r\n$7\r\nmessage\r\n$6\r\nevents\r\n$5\r\nhello\r\n");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), std::size_t{4});
    ASSERT_EQ(static_cast<int>(parsed->at(0).data_type), static_cast<int>(resp3_type::push));
    ASSERT_EQ(parsed->at(0).aggregate_size, std::size_t{3});
    ASSERT_EQ(parsed->at(1).value, std::string("message"));
    ASSERT_EQ(parsed->at(2).value, std::string("events"));
    ASSERT_EQ(parsed->at(3).value, std::string("hello"));
}

TEST(redis_cluster_slots_parse_and_cache_ipv6)
{
    std::vector<resp3_node> nodes{
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 2},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 4},
        resp3_node{.data_type = resp3_type::number, .value = "0"},
        resp3_node{.data_type = resp3_type::number, .value = "8191"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::blob_string, .value = "127.0.0.1"},
        resp3_node{.data_type = resp3_type::number, .value = "7000"},
        resp3_node{.data_type = resp3_type::blob_string, .value = "node-a"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::blob_string, .value = "127.0.0.1"},
        resp3_node{.data_type = resp3_type::number, .value = "7003"},
        resp3_node{.data_type = resp3_type::blob_string, .value = "node-a-replica"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::number, .value = "8192"},
        resp3_node{.data_type = resp3_type::number, .value = "16383"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::blob_string, .value = "2001:db8::10"},
        resp3_node{.data_type = resp3_type::number, .value = "7001"},
        resp3_node{.data_type = resp3_type::blob_string, .value = "node-b"},
    };

    auto ranges = client::parse_cluster_slots(nodes);
    ASSERT_TRUE(ranges.has_value());
    ASSERT_EQ(ranges->size(), std::size_t{2});
    ASSERT_EQ(ranges->at(0).start, 0);
    ASSERT_EQ(ranges->at(0).end, 8191);
    ASSERT_EQ(ranges->at(0).master.host, std::string("127.0.0.1"));
    ASSERT_EQ(ranges->at(0).master.port, 7000);
    ASSERT_EQ(ranges->at(0).replicas.size(), std::size_t{1});
    ASSERT_EQ(ranges->at(1).master.host, std::string("2001:db8::10"));
    ASSERT_EQ(ranges->at(1).master.port, 7001);

    cluster_slot_cache cache;
    cache.update(*ranges);
    ASSERT_EQ(cache.covered_slots(), std::size_t{16384});
    auto first = cache.endpoint_for_slot(1);
    auto last = cache.endpoint_for_slot(16383);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(last.has_value());
    ASSERT_EQ(first->port, 7000);
    ASSERT_EQ(last->host, std::string("2001:db8::10"));
}

RUN_TESTS()
