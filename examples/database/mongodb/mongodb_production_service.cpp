#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mongodb;

// These implementation headers form the example's explicit application
// layering. Keep dependency order visible instead of alphabetically sorting it.
// clang-format off
#include "mongodb_service_config.hpp"
#include "mongodb_event_repository.hpp"
#include "mongodb_transaction_service.hpp"
#include "mongodb_change_stream_listener.hpp"
#include "mongodb_health_indicator.hpp"
#include "mongodb_worker_service.hpp"
#include "mongodb_shutdown_signal.hpp"
#include "mongodb_service_application.hpp"
#include "mongodb_example_scenario_runner.hpp"
// clang-format on

namespace {
auto run_guarded(cnetmod::io_context& context, cnetmod::task<void> operation,
    std::atomic_int& exit_code) -> cnetmod::task<void>
{
    try
    {
        co_await std::move(operation);
    }
    catch (const std::exception& error)
    {
        logger::critical("MongoDB production service failed: {}", error.what());
        exit_code.store(1, std::memory_order_release);
        context.stop();
    }
    catch (...)
    {
        logger::critical("MongoDB production service failed with an unknown exception");
        exit_code.store(1, std::memory_order_release);
        context.stop();
    }
}
} // namespace

auto main(int argc, char** argv) -> int
{
    logger::init("mongodb-production-service", logger::level::info);
    try
    {
        cnetmod::net_init network;
        auto context = cnetmod::make_io_context();
        auto config = mongodb_example::service_config::from_environment();
        std::optional<std::string> scenario;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            if (argument == "--scenario" && index + 1 < argc)
                scenario = argv[++index];
            else
                throw std::invalid_argument("usage: mongodb_production_service [--scenario <name>]");
        }
        std::unique_ptr<mongodb_example::service_application> application;
        std::unique_ptr<mongodb_example::example_scenario_runner> runner;
        std::atomic_int exit_code{0};
        if (scenario)
        {
            runner = std::make_unique<mongodb_example::example_scenario_runner>(
                *context, std::move(config), *scenario);
            cnetmod::spawn(*context,
                run_guarded(*context, runner->run(), exit_code));
        }
        else
        {
            application = std::make_unique<mongodb_example::service_application>(
                *context, std::move(config));
            cnetmod::spawn(*context,
                run_guarded(*context, application->start(), exit_code));
        }
        context->run();
        const auto result = exit_code.load(std::memory_order_acquire);
        logger::shutdown();
        return result;
    }
    catch (const std::exception& error)
    {
        logger::critical("MongoDB service terminated: {}", error.what());
        logger::shutdown();
        return 1;
    }
}
