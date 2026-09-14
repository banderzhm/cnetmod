#include "test_framework.hpp"

import std;
import cnetmod.application.redis;
import cnetmod.application.host;
import cnetmod.application.configuration;
import cnetmod.application.recovery_policy;
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.observability;
import cnetmod.protocol.redis;
import cnetmod.protocol.http;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

namespace application = cnetmod::application;

#include "application_redis_host_cleanup_cases.inc"

TEST(application_redis_health_probes_the_peer_with_a_deadline)
{
    for (int mode = 0; mode < 3; ++mode)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        cnetmod::observability::telemetry_hub telemetry{*io, {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::task_supervisor supervisor{*io};
        application::redis_service service{*io, {.host = "127.0.0.1", .port = endpoint->port(), .resp3 = false, .initial_size = 1, .max_size = 1, .ping_interval = std::chrono::hours{1}},
            "test", application::service_requirement::required, {}};
        bool correct = false, wire_correct = false, stopped = false;
        bool acquisition_safe = true;
        bool supervisor_stop_completed = true;
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
            constexpr std::string_view command = "*1\r\n$4\r\nPING\r\n";
            std::array<char, command.size()> bytes{};
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                auto received = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{bytes.data() + offset, bytes.size() - offset});
                if (!received || *received == 0U)
                    break;
                offset += *received;
            }
            wire_correct = std::string_view{bytes.data(), offset} == command;
            if (mode != 2)
            {
                const std::string_view reply = mode == 0 ? "+PONG\r\n" : "-private-credential-detail\r\n";
                (void)co_await cnetmod::async_write_all(*io, *peer,
                    cnetmod::const_buffer{reply.data(), reply.size()});
            }
            else
            {
                char byte{};
                auto received = co_await cnetmod::async_read(*io, *peer, cnetmod::mutable_buffer{&byte, 1});
                wire_correct = wire_correct && (!received || *received == 0U);
            }
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            cnetmod::cancel_token startup;
            application::service_context context{*io, telemetry, supervisor, startup,
                cnetmod::deadline::after(std::chrono::seconds{2})};
            auto started = co_await service.start(context);
            if (!started)
            {
                io->stop();
                co_return;
            }
            if (mode == 0)
            {
                auto occupied = service.pool().try_get_connection();
                if (!occupied)
                {
                    io->stop();
                    co_return;
                }
                cnetmod::cancel_token saturated_token;
                application::service_context saturated_context{*io, telemetry, supervisor, saturated_token,
                    cnetmod::deadline::after(std::chrono::milliseconds{50})};
                auto saturated = co_await service.probe(saturated_context);
                acquisition_safe = saturated.status == application::service_health::down &&
                    saturated.error == std::errc::timed_out &&
                    saturated.message == "redis connection unavailable" &&
                    occupied->get().is_open() && service.pool().waiter_count() == 0;
                occupied = std::unexpected(std::make_error_code(std::errc::operation_canceled));

                // Exercise both completion-claim orders with a live pooled socket.
                for (bool cancel_first : {false, true})
                {
                    auto held = service.pool().try_get_connection();
                    if (!held)
                    {
                        acquisition_safe = false;
                        break;
                    }
                    cnetmod::cancel_token competing_token;
                    auto competing = service.pool().async_get_connection(competing_token);
                    competing.handle().resume();
                    acquisition_safe = acquisition_safe && service.pool().waiter_count() == 1;
                    if (cancel_first)
                        competing_token.cancel();
                    *held = cnetmod::redis::pooled_connection{};
                    if (!cancel_first)
                        competing_token.cancel();
                    while (!competing.handle().done())
                        co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
                    {
                        auto result = competing.handle().promise().result();
                        if (cancel_first)
                            acquisition_safe = acquisition_safe && !result &&
                                result.error() == std::errc::operation_canceled;
                        else
                            acquisition_safe = acquisition_safe && result && result->get().is_open();
                    }
                    acquisition_safe = acquisition_safe && service.pool().waiter_count() == 0 &&
                        service.pool().idle_count() == 1;
                }

                cnetmod::cancel_token cancelled_token;
                cancelled_token.cancel();
                application::service_context cancelled_context{*io, telemetry, supervisor, cancelled_token,
                    cnetmod::deadline::after(std::chrono::seconds{1})};
                auto cancelled = co_await service.probe(cancelled_context);
                acquisition_safe = acquisition_safe &&
                    cancelled.status == application::service_health::down &&
                    cancelled.error == std::errc::operation_canceled &&
                    service.pool().idle_count() == 1 && service.pool().waiter_count() == 0;
            }
            cnetmod::cancel_token probe_token;
            application::service_context probe_context{*io, telemetry, supervisor, probe_token,
                cnetmod::deadline::after(std::chrono::milliseconds{150})};
            auto report = co_await service.probe(probe_context);
            correct = report.status == (mode == 0 ? application::service_health::up : application::service_health::down);
            correct = correct && report.message.find("private") == std::string::npos;
            if (mode == 2)
                correct = correct && report.error == std::errc::timed_out;
            if (mode == 1)
                correct = correct && report.error == std::errc::protocol_error;
            if (mode == 0)
            {
                auto retained = service.pool().try_get_connection();
                ASSERT_TRUE(retained.has_value());
                ASSERT_EQ(service.pool().checked_out_count(), 1U);
                supervisor.request_stop();
                const auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{200};
                while (supervisor.state("redis-pool:test") != application::supervised_task_state::stopped &&
                    std::chrono::steady_clock::now() < stop_deadline)
                    co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{5});
                supervisor_stop_completed = supervisor.state("redis-pool:test") == application::supervised_task_state::stopped &&
                    service.pool().pending_maintenance() == 0;
                cnetmod::cancel_token cleanup_token;
                application::service_context cleanup_context{*io, telemetry, supervisor, cleanup_token,
                    cnetmod::deadline::after(std::chrono::milliseconds{30})};
                auto pending_stop = co_await service.stop(cleanup_context);
                ASSERT_FALSE(pending_stop.has_value());
                ASSERT_EQ(pending_stop.error(), std::make_error_code(std::errc::timed_out));
                ASSERT_EQ(service.pool().checked_out_count(), 1U);
                ASSERT_TRUE(retained->valid());
                *retained = cnetmod::redis::pooled_connection{};
                ASSERT_EQ(service.pool().checked_out_count(), 0U);
            }
            auto stop_result = co_await service.stop(context);
            supervisor.request_stop();
            auto joined = co_await supervisor.join();
            stopped = stop_result && joined && service.pool().pending_maintenance() == 0;
            finish();
        };
        cnetmod::spawn(*io, broker());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(correct);
        ASSERT_TRUE(wire_correct);
        ASSERT_TRUE(stopped);
        ASSERT_TRUE(acquisition_safe);
        ASSERT_TRUE(supervisor_stop_completed);
    }
}

TEST(application_redis_stop_before_dispatch_does_not_start_maintenance)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::redis::connection_pool pool{*io, {}};
    application::task_supervisor supervisor{*io};
    auto registered = supervisor.supervise("pool", [&](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_await pool.async_run();
            co_return {};
        },
        {}, true, [&]() noexcept
        {
            pool.request_stop();
        });
    ASSERT_TRUE(registered.has_value());
    supervisor.request_stop();
    supervisor.request_stop();
    bool joined = false;
    auto finish = [&]() -> cnetmod::task<void>
    {
        joined = (co_await supervisor.join()).has_value();
        io->stop();
    };
    cnetmod::spawn(*io, finish());
    io->run();
    ASSERT_TRUE(joined);
    ASSERT_EQ(pool.size(), 0U);
    ASSERT_EQ(pool.pending_maintenance(), 0);
}

RUN_TESTS()
