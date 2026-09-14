#include "test_framework.hpp"

import std;
import cnetmod.application.redis;
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.observability;
import cnetmod.protocol.redis;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

namespace {
std::uint16_t test_port{};
}

TEST(redis_live_resp3_health_and_supervised_stop)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::redis::pool_params options;
    options.host = "127.0.0.1";
    options.port = test_port;
    options.initial_size = options.max_size = 1;
    options.resp3 = true;
    options.ping_interval = std::chrono::hours{1};
    cnetmod::application::redis_service service{*io, options, "live",
        cnetmod::application::service_requirement::required, {}};
    bool completed = false;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token cancellation;
        cnetmod::application::service_context context{*io, telemetry, supervisor, cancellation,
            cnetmod::deadline::after(std::chrono::seconds{5})};
        const auto started = co_await cnetmod::with_deadline(*io, context.operation_deadline,
            service.start(context), cancellation);
        ASSERT_TRUE(started.has_value());
        if (started)
        {
            for (unsigned index = 0; index < 3; ++index)
            {
                auto health = co_await service.probe(context);
                ASSERT_TRUE(health.status == cnetmod::application::service_health::up);
                ASSERT_FALSE(static_cast<bool>(health.error));
            }
            for (unsigned index = 0; index < 3; ++index)
            {
                {
                    auto lease = co_await service.pool().async_get_connection(context.cancellation);
                    ASSERT_TRUE(lease.has_value());
                    lease->get().close();
                    ASSERT_FALSE(lease->get().is_open());
                }
                context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{5});
                auto recovered = co_await service.probe(context);
                ASSERT_TRUE(recovered.status == cnetmod::application::service_health::up);
                ASSERT_FALSE(static_cast<bool>(recovered.error));
                ASSERT_EQ(service.pool().size(), 1U);
                ASSERT_EQ(service.pool().waiter_count(), 0U);
            }
        }
        ASSERT_TRUE((co_await service.stop(context)).has_value());
        supervisor.request_stop();
        ASSERT_TRUE((co_await supervisor.join()).has_value());
        ASSERT_EQ(service.pool().waiter_count(), 0U);
        ASSERT_EQ(service.pool().pending_maintenance(), 0);
        auto stopped = co_await service.probe(context);
        ASSERT_TRUE(stopped.status == cnetmod::application::service_health::down);
        completed = true;
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
    ASSERT_TRUE(completed);
}

int main()
{
    const auto* enabled = std::getenv("CNETMOD_REDIS_INTEGRATION");
    if (!enabled || std::string_view{enabled} != "1")
        return 77;
    const auto* configured = std::getenv("CNETMOD_REDIS_TEST_PORT");
    if (!configured)
        return EXIT_FAILURE;
    const std::string_view value{configured};
    unsigned port{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || port == 0 || port > 65535)
        return EXIT_FAILURE;
    test_port = static_cast<std::uint16_t>(port);
    return cnetmod::test::run_all();
}
