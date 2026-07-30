#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.utils;
import cnetmod.protocol.tcp;
import cnetmod.protocol.postgresql;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.access_log;
import cnetmod.protocol.http.middleware.body_limit;
import cnetmod.protocol.http.middleware.graceful_shutdown;
import cnetmod.protocol.http.middleware.recover;
import cnetmod.protocol.http.middleware.request_id;
import nlohmann.json;

#include <cnetmod/orm.hpp>

// These implementation headers form the example's explicit application
// layering. Keep dependency order visible instead of alphabetically sorting it.
// clang-format off
#include "postgresql_service_config.hpp"
#include "postgresql_service_health.hpp"
#include "postgresql_failover_connection_manager.hpp"
#include "postgresql_request_repository.hpp"
#include "postgresql_transaction_boundary.hpp"
#include "postgresql_application_error.hpp"
#include "postgresql_request_application_service.hpp"
#include "postgresql_http_response_mapper.hpp"
#include "postgresql_http_request_controller.hpp"
#include "postgresql_service_application.hpp"
// clang-format on

auto main() -> int
{
    logger::init("postgresql-production-service", logger::level::info);
    try
    {
        cnetmod::net_init network;
        auto context = cnetmod::make_io_context();
        postgresql_example::service_application application(
            *context, postgresql_example::service_config::from_environment());
        cnetmod::spawn(*context, application.start());
        context->run();
        const auto exit_code = application.exit_code();
        logger::shutdown();
        return exit_code;
    }
    catch (const std::exception& error)
    {
        logger::critical("PostgreSQL service terminated: {}", error.what());
        logger::shutdown();
        return 1;
    }
}
