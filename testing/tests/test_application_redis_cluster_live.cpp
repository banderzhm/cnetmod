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
std::uint16_t seed_port{};
}

TEST(redis_cluster_live_routes_health_and_ordered_pipeline)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::redis::connect_options seed;
    seed.host = "127.0.0.1";
    seed.port = seed_port;
    cnetmod::application::redis_cluster_service service{*io, {seed}, "live",
        cnetmod::application::service_requirement::required, {}};

    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token cancellation;
        cnetmod::application::service_context context{*io, telemetry,
            supervisor, cancellation,
            cnetmod::deadline::after(std::chrono::seconds{10})};
        const auto started = co_await service.start(context);
        ASSERT_TRUE(started.has_value());
        if (!started)
        {
            io->stop();
            co_return;
        }
        ASSERT_EQ(service.client().slots().covered_slots(), 16384U);
        const auto health = co_await service.probe(context);
        ASSERT_TRUE(health.status ==
            cnetmod::application::service_health::up);

        std::vector<cnetmod::redis::cluster_pipeline_item> commands;
        for (unsigned index = 0; index < 12; ++index)
        {
            const auto key = std::format("cnetmod:cluster:{}", index);
            commands.push_back({{"SET", key, std::to_string(index)}, key});
        }
        for (unsigned index = 0; index < 12; ++index)
        {
            const auto key = std::format("cnetmod:cluster:{}", index);
            commands.push_back({{"GET", key}, key});
        }
        auto responses = co_await service.client().pipeline_ordered(
            commands, cancellation);
        ASSERT_TRUE(responses.has_value());
        if (responses)
        {
            ASSERT_EQ(responses->size(), commands.size());
            for (unsigned index = 0; index < 12; ++index)
                ASSERT_TRUE(cnetmod::redis::is_ok((*responses)[index]));
            for (unsigned index = 0; index < 12; ++index)
                ASSERT_EQ(cnetmod::redis::first_value(
                              (*responses)[12 + index]),
                    std::to_string(index));
        }

        ASSERT_TRUE((co_await service.stop(context)).has_value());
        supervisor.request_stop();
        ASSERT_TRUE((co_await supervisor.join()).has_value());
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

int main()
{
    const auto* enabled = std::getenv("CNETMOD_REDIS_CLUSTER_INTEGRATION");
    if (!enabled || std::string_view{enabled} != "1")
        return 77;
    const auto* configured = std::getenv("CNETMOD_REDIS_CLUSTER_SEED_PORT");
    if (!configured)
        return EXIT_FAILURE;
    const std::string_view value{configured};
    unsigned port{};
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), port);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() ||
        port == 0 || port > 65535)
        return EXIT_FAILURE;
    seed_port = static_cast<std::uint16_t>(port);
    return cnetmod::test::run_all();
}
