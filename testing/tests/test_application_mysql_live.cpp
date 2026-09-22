#include "test_framework.hpp"

import std;
import cnetmod.application;
import cnetmod.protocol.mysql;
import cnetmod.core;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.http;
import cnetmod.executor.async_op;
import cnetmod.json;
#ifdef CNETMOD_TEST_HAS_ORM
import cnetmod.orm;
import cnetmod.orm.database_session;
import cnetmod.instrumentation.tracing;
#endif

namespace {

std::uint16_t mysql_test_port = 3306;
std::string mysql_test_host = "127.0.0.1";
} // namespace

TEST(mysql_live_authentication_health_and_supervised_stop)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::mysql::pool_params options;
    options.host = mysql_test_host;
    options.port = mysql_test_port;
    options.username = std::getenv("CNETMOD_MYSQL_TEST_USER");
    options.password = std::getenv("CNETMOD_MYSQL_TEST_PASSWORD");
    options.database = std::getenv("CNETMOD_MYSQL_TEST_DATABASE");
    // MySQL 8.4 uses caching_sha2_password by default.  Require TLS so the
    // full authentication exchange never sends credentials in clear text.
    // The ephemeral CI server uses its generated self-signed certificate.
    options.ssl = cnetmod::mysql::ssl_mode::require;
    options.tls_verify = false;
    options.initial_size = 1;
    options.max_size = 1;
    cnetmod::application::mysql_service service{*io, options, "live",
        cnetmod::application::service_requirement::required, {}};
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token startup;
        cnetmod::application::service_context start_context{*io, telemetry, supervisor, startup,
            cnetmod::deadline::after(std::chrono::seconds{5})};
        auto started = co_await service.start(start_context);
        ASSERT_TRUE(started.has_value());
        if (started)
        {
            for (unsigned check = 0; check < 3; ++check)
            {
                cnetmod::cancel_token cancellation;
                cnetmod::application::service_context context{*io, telemetry, supervisor, cancellation,
                    cnetmod::deadline::after(std::chrono::seconds{3})};
                auto health = co_await service.probe(context);
                ASSERT_TRUE(health.status == cnetmod::application::service_health::up);
                ASSERT_FALSE(static_cast<bool>(health.error));
            }
        }
        std::optional<cnetmod::mysql::pooled_connection> borrowed;
        if (started)
        {
            auto acquired = co_await service.pool().async_get_connection(
                cnetmod::deadline::after(std::chrono::seconds{3}));
            ASSERT_TRUE(acquired.has_value());
            if (acquired)
                borrowed.emplace(std::move(*acquired));
        }
        if (borrowed)
        {
            auto marker = co_await borrowed->get().query("SET @cnetmod_lease_marker = 42");
            ASSERT_FALSE(marker.is_err());
            borrowed->return_without_reset();
            auto retained_lease = service.pool().try_get_connection();
            ASSERT_TRUE(retained_lease.has_value());
            if (retained_lease)
            {
                borrowed.emplace(std::move(*retained_lease));
                auto retained = co_await borrowed->get().query("SELECT @cnetmod_lease_marker");
                ASSERT_EQ(retained.rows.size(), 1U);
                if (retained.rows.size() == 1 && !retained.rows.front().empty())
                    ASSERT_EQ(retained.rows.front().front().to_string(), "42");
            }
            borrowed.reset();
            auto next_lease = co_await service.pool().async_get_connection(
                cnetmod::deadline::after(std::chrono::seconds{3}));
            ASSERT_TRUE(next_lease.has_value());
            if (next_lease)
            {
                borrowed.emplace(std::move(*next_lease));
                auto isolated = co_await borrowed->get().query("SELECT @cnetmod_lease_marker IS NULL");
                ASSERT_EQ(isolated.rows.size(), 1U);
                if (isolated.rows.size() == 1 && !isolated.rows.front().empty())
                    ASSERT_EQ(isolated.rows.front().front().to_string(), "1");
            }
        }
        unsigned graceful_recoveries = 0;
        unsigned killed_recoveries = 0;
        if (borrowed)
        {
            for (unsigned cycle = 0; cycle < 6; ++cycle)
            {
                auto before = co_await borrowed->get().query("SELECT CONNECTION_ID()");
                ASSERT_TRUE(before.has_rows());
                if (before.rows.empty() || before.rows.front().empty())
                    break;
                const auto previous_id = before.rows.front().front().to_string();
                auto marker = co_await borrowed->get().query("SET @cnetmod_recovery_marker = 1");
                ASSERT_FALSE(marker.is_err());
                if (cycle < 3)
                {
                    co_await borrowed->get().quit();
                }
                else
                {
                    std::uint64_t connection_id{};
                    const auto [end, error] = std::from_chars(previous_id.data(),
                        previous_id.data() + previous_id.size(), connection_id);
                    ASSERT_TRUE(error == std::errc{});
                    ASSERT_TRUE(end == previous_id.data() + previous_id.size());
                    ASSERT_NE(connection_id, 0U);
                    if (error != std::errc{} || end != previous_id.data() + previous_id.size() || connection_id == 0)
                        break;
                    cnetmod::mysql::client control{*io};
                    cnetmod::mysql::connect_options control_options;
                    control_options.host = options.host;
                    control_options.port = options.port;
                    control_options.username = options.username;
                    control_options.password = options.password;
                    control_options.database = options.database;
                    control_options.ssl = cnetmod::mysql::ssl_mode::require;
                    control_options.tls_verify = false;
                    auto connected = co_await control.connect(std::move(control_options));
                    ASSERT_FALSE(connected.is_err());
                    if (connected.is_err())
                        break;
                    // Only terminate the session owned by this test's retained lease.
                    auto killed = co_await control.query(std::format("KILL CONNECTION {}", connection_id));
                    ASSERT_FALSE(killed.is_err());
                    co_await control.quit();
                    if (killed.is_err())
                        break;
                    auto interrupted = co_await cnetmod::with_deadline(*io,
                        cnetmod::deadline::after(std::chrono::seconds{3}),
                        [&](cnetmod::cancel_token& cancellation)
                            -> cnetmod::task<std::expected<void, std::error_code>>
                        {
                            auto ping = co_await borrowed->get().ping(cancellation);
                            if (ping.is_err())
                                co_return std::unexpected(borrowed->get().last_error());
                            co_return std::expected<void, std::error_code>{};
                        });
                    ASSERT_FALSE(interrupted.has_value());
                    if (!interrupted)
                        ASSERT_FALSE(interrupted.error() == std::errc::timed_out);
                    ASSERT_TRUE(static_cast<bool>(borrowed->get().last_error()));
                }
                ASSERT_FALSE(borrowed->get().is_open());
                borrowed.reset();

                auto replacement = co_await service.pool().async_get_connection(
                    cnetmod::deadline::after(std::chrono::seconds{3}));
                ASSERT_TRUE(replacement.has_value());
                if (!replacement)
                    break;
                borrowed.emplace(std::move(*replacement));
                auto after = co_await borrowed->get().query(
                    "SELECT CONNECTION_ID(), @cnetmod_recovery_marker IS NULL");
                ASSERT_EQ(after.rows.size(), 1U);
                if (after.rows.size() == 1)
                {
                    ASSERT_EQ(after.rows.front().size(), 2U);
                    if (after.rows.front().size() == 2)
                    {
                        ASSERT_NE(after.rows.front()[0].to_string(), previous_id);
                        ASSERT_EQ(after.rows.front()[1].to_string(), "1");
                    }
                }
                ASSERT_EQ(service.pool().size(), 1U);
                ASSERT_EQ(service.pool().waiter_count(), 0U);
                if (cycle < 3)
                    ++graceful_recoveries;
                else
                    ++killed_recoveries;
            }
        }
        ASSERT_EQ(graceful_recoveries, 3U);
        ASSERT_EQ(killed_recoveries, 3U);
        // The service remains alive while a lease is held. Maintenance must
        // join without waiting indefinitely for application-owned leases.
#ifdef CNETMOD_TEST_HAS_ORM
        if (borrowed)
        {
            cnetmod::orm::database_session session{borrowed->get()};
            const auto parent = cnetmod::instrumentation::new_root_context();
            std::vector<cnetmod::instrumentation::completed_span> spans;
            cnetmod::instrumentation::span_exporter enabled = [&](const auto& span)
            {
                spans.push_back(span);
            };
            cnetmod::instrumentation::span_exporter disabled;
            cnetmod::instrumentation::span_exporter failing = [](const auto&)
            {
                throw std::runtime_error{"test exporter unavailable"};
            };
            for (const auto sql : {"SELECT 'private-result' AS value", "SELECT cnetmod_missing_column"})
            {
                const auto baseline = co_await session.query(sql);
                const bool expect_success = std::string_view{sql}.contains("private-result");
                ASSERT_EQ(baseline.ok(), expect_success);
                if (expect_success)
                {
                    ASSERT_EQ(baseline.rows.size(), 1U);
                    if (!baseline.rows.empty())
                    {
                        ASSERT_EQ(baseline.rows.front().size(), 1U);
                        if (!baseline.rows.front().empty())
                            ASSERT_EQ(baseline.rows.front().front().as_string(), "private-result");
                    }
                }
                else
                {
                    ASSERT_EQ(baseline.error_code, 1054U);
                    ASSERT_EQ(baseline.sql_state, "42S22");
                }
                for (const auto* sink : {&disabled, &enabled, &failing})
                {
                    const auto result = co_await session.query(sql, parent, *sink);
                    ASSERT_EQ(result.ok(), baseline.ok());
                    ASSERT_EQ(result.error_code, baseline.error_code);
                    ASSERT_EQ(result.sql_state, baseline.sql_state);
                    ASSERT_EQ(result.error_msg, baseline.error_msg);
                    ASSERT_EQ(result.rows.size(), baseline.rows.size());
                    if (!result.rows.empty() && !baseline.rows.empty())
                    {
                        ASSERT_EQ(result.rows.front().size(), baseline.rows.front().size());
                        if (!result.rows.front().empty() && !baseline.rows.front().empty())
                            ASSERT_EQ(result.rows.front().front().as_string(), baseline.rows.front().front().as_string());
                    }
                }
            }
            ASSERT_EQ(spans.size(), 2U);
            if (spans.size() == 2)
            {
                ASSERT_FALSE(spans.front().failed);
                ASSERT_TRUE(spans.back().failed);
            }
            for (const auto& span : spans)
            {
                ASSERT_EQ(span.context.trace_id, parent.trace_id);
                ASSERT_EQ(span.parent_span_id, parent.span_id);
                ASSERT_NE(span.context.span_id, parent.span_id);
                for (const auto& [key, value] : span.attributes)
                {
                    ASSERT_NE(key, "db.query.text");
                    ASSERT_FALSE(value.contains("private-result"));
                    ASSERT_FALSE(value.contains("cnetmod_missing_column"));
                }
            }
        }
#endif
        supervisor.request_stop();
        ASSERT_TRUE((co_await supervisor.join()).has_value());
        cnetmod::cancel_token stopping;
        cnetmod::application::service_context stop_context{*io, telemetry, supervisor, stopping,
            cnetmod::deadline::after(std::chrono::milliseconds{30})};
        const auto first_stop = co_await service.stop(stop_context);
        ASSERT_EQ(first_stop.has_value(), !borrowed.has_value());
        if (borrowed && !first_stop)
            ASSERT_TRUE(first_stop.error() == std::errc::timed_out);
        ASSERT_EQ(service.pool().checked_out_count(), borrowed ? 1U : 0U);
        if (borrowed)
            ASSERT_TRUE(borrowed->valid());
        auto after_stop = co_await service.pool().async_get_connection(
            cnetmod::deadline::after(std::chrono::seconds{1}));
        ASSERT_FALSE(after_stop.has_value());
        if (!after_stop)
            ASSERT_TRUE(after_stop.error() == std::errc::operation_canceled);
        stop_context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{1});
        auto* returned_client = borrowed ? &borrowed->get() : nullptr;
        auto delayed_stop = service.stop(stop_context);
        delayed_stop.handle().resume();
        if (borrowed)
            ASSERT_FALSE(delayed_stop.handle().done());
        borrowed.reset();
        ASSERT_EQ(service.pool().checked_out_count(), 0U);
        const auto cleanup_limit = cnetmod::deadline::after(std::chrono::seconds{2});
        while (!delayed_stop.handle().done() && !cleanup_limit.expired())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(delayed_stop.handle().done());
        if (!delayed_stop.handle().done())
            std::terminate();
        ASSERT_TRUE(delayed_stop.handle().promise().result().has_value());
        if (returned_client)
            ASSERT_FALSE(returned_client->is_open());
        ASSERT_EQ(service.pool().waiter_count(), 0U);
        auto stopped = co_await service.probe(start_context);
        ASSERT_TRUE(stopped.status == cnetmod::application::service_health::down);
        completed = true;
    };
    auto guarded = [&]() -> cnetmod::task<void>
    {
        std::exception_ptr failure;
        try
        {
            co_await run();
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (failure)
        {
            supervisor.request_stop();
            try
            {
                (void)co_await supervisor.join();
                cnetmod::cancel_token cancellation;
                cnetmod::application::service_context context{*io, telemetry, supervisor, cancellation,
                    cnetmod::deadline::after(std::chrono::seconds{5})};
                (void)co_await service.stop(context);
            }
            catch (...)
            {
                // Preserve the initiating exception for the test harness.
            }
        }
        io->stop();
        if (failure)
            std::rethrow_exception(failure);
    };
    auto work = guarded();
    io->post(work.handle());
    io->run();
    ASSERT_TRUE(work.handle().done());
    work.handle().promise().result();
    ASSERT_TRUE(completed);
}

static void verify_mysql_host_lease_cleanup(unsigned collector_mode)
{
    namespace app = cnetmod::application;

    struct lease_holder final : app::managed_service
    {
        app::mysql_service* database = nullptr;
        std::optional<cnetmod::mysql::pooled_connection> lease;
        unsigned starts = 0;
        unsigned stops = 0;

        auto key() const -> app::service_key override
        {
            return {"lease-holder"};
        }

        auto dependencies() const -> std::vector<app::service_key> override
        {
            return {{"mysql", "default"}};
        }

        auto requirement() const noexcept -> app::service_requirement override
        {
            return app::service_requirement::required;
        }

        auto start(app::service_context& context) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++starts;
            auto acquired = co_await database->pool().async_get_connection(context.operation_deadline);
            if (!acquired)
                co_return std::unexpected(acquired.error());
            lease.emplace(std::move(*acquired));
            co_return std::expected<void, std::error_code>{};
        }

        auto stop(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++stops;
            // Deliberately retain the externally owned lease across Host return.
            co_return std::expected<void, std::error_code>{};
        }

        auto probe(app::service_context&) -> cnetmod::task<app::health_report> override
        {
            co_return app::health_report{.status = app::service_health::up};
        }
    };

    cnetmod::net_init network;
    auto occupied = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(occupied.has_value());
    if (!occupied)
        return;
    ASSERT_TRUE(occupied->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(occupied->listen().has_value());
    const auto endpoint = occupied->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    auto probe_io = cnetmod::make_io_context();
    cnetmod::http::server bind_probe{*probe_io};
    const auto bind_error = bind_probe.listen("127.0.0.1", endpoint->port());
    ASSERT_FALSE(bind_error.has_value());
    if (bind_error)
        return;
    auto collector = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(collector.has_value());
    if (!collector)
        return;
    ASSERT_TRUE(collector->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto collector_endpoint = collector->local_endpoint();
    ASSERT_TRUE(collector_endpoint.has_value());
    if (!collector_endpoint)
        return;
    const bool observation_enabled = collector_mode != 0;
    cnetmod::http::server receiver{*probe_io};
    std::array<unsigned, 3> received{};
    std::vector<std::pair<std::string, std::string>> mysql_start_spans;
    std::vector<std::pair<std::string, std::string>> mysql_start_logs;
    bool mysql_start_metric = false;
    bool invalid_payload = false;
    cnetmod::task<void> accept;
    std::jthread receiver_thread;
    if (collector_mode == 2)
    {
        cnetmod::http::router routes;
        routes.post("/v1/:signal", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
            {
                try
                {
                    const auto body = std::string{co_await request.read_full_body()};
                    const std::string_view password{std::getenv("CNETMOD_MYSQL_TEST_PASSWORD")};
                    invalid_payload |= !password.empty() && body.contains(password);
                    const auto document = cnetmod::json::parse_document(body).value();
                    const unsigned signal = request.path() == "/v1/traces" ? 0U
                        : request.path() == "/v1/metrics"                  ? 1U
                                                                           : 2U;
                    const std::array keys{"resourceSpans", "resourceMetrics", "resourceLogs"};
                    invalid_payload |= !document.contains(keys[signal]) || document.at(keys[signal]).empty();
                    const auto attributes = [](const cnetmod::json::document& record)
                    {
                        std::map<std::string, std::string> result;
                        for (const auto& attribute : record.at("attributes").get_array())
                            result.emplace(attribute.at("key").get<std::string>(),
                                attribute.at("value").at("stringValue").get<std::string>());
                        return result;
                    };
                    if (signal == 0)
                    {
                        for (const auto& resource : document.at("resourceSpans").get_array())
                            for (const auto& scope : resource.at("scopeSpans").get_array())
                                for (const auto& span : scope.at("spans").get_array())
                                {
                                    const auto labels = attributes(span);
                                    if (labels.contains("service.name") && labels.at("service.name") == "mysql" &&
                                        span.at("name").get<std::string>() == "application.service.start")
                                    {
                                        invalid_payload |= labels.at("service.instance") != "default";
                                        invalid_payload |= span.at("kind").get<std::string>() != "SPAN_KIND_INTERNAL";
                                        mysql_start_spans.emplace_back(span.at("traceId").get<std::string>(),
                                            span.at("spanId").get<std::string>());
                                    }
                                }
                    }
                    else if (signal == 2)
                    {
                        for (const auto& resource : document.at("resourceLogs").get_array())
                            for (const auto& scope : resource.at("scopeLogs").get_array())
                                for (const auto& record : scope.at("logRecords").get_array())
                                {
                                    const auto labels = attributes(record);
                                    if (labels.at("service.name") == "mysql" && labels.at("operation") == "start")
                                    {
                                        invalid_payload |= labels.at("service.instance") != "default" || labels.at("outcome") != "success";
                                        mysql_start_logs.emplace_back(record.at("traceId").get<std::string>(),
                                            record.at("spanId").get<std::string>());
                                    }
                                }
                    }
                    else
                    {
                        for (const auto& resource : document.at("resourceMetrics").get_array())
                            for (const auto& scope : resource.at("scopeMetrics").get_array())
                                for (const auto& metric : scope.at("metrics").get_array())
                                {
                                    if (metric.at("name").get<std::string>() != "application.service.operations")
                                        continue;
                                    for (const auto& point : metric.at("sum").at("dataPoints").get_array())
                                    {
                                        const auto labels = attributes(point);
                                        if (labels.at("service.name") == "mysql" && labels.at("operation") == "start" &&
                                            labels.at("outcome") == "success" && labels.at("service.instance") == "default")
                                            mysql_start_metric |= point.at("asDouble").as<double>() >= 1.0;
                                    }
                                }
                    }
                    ++received[signal];
                    request.json(cnetmod::http::status::ok, "{}");
                }
                catch (...)
                {
                    invalid_payload = true;
                    request.json(cnetmod::http::status::bad_request, "{}");
                }
            });
        receiver.set_router(std::move(routes));
        collector->close();
        const auto listening = receiver.listen("127.0.0.1", collector_endpoint->port());
        ASSERT_TRUE(listening.has_value());
        if (!listening)
            return;
        accept = receiver.run();
        accept.handle().resume();
        receiver_thread = std::jthread{[&]
            {
                probe_io->run();
            }};
    }
    auto holder = std::make_shared<lease_holder>();
    auto host = app::application_builder{"mysql-lease-retry"}
                    .configure([&](auto& value)
                        {
                            value.http.address = "127.0.0.1";
                            value.http.port = endpoint->port();
                            value.management.enabled = false;
                            value.logging.manage_lifecycle = false;
                            value.install_signal_handlers = false;
                            value.observability.tracing = observation_enabled;
                            value.observability.metrics = observation_enabled;
                            value.observability.logs = observation_enabled;
                            if (observation_enabled)
                            {
                                value.observability.otlp.endpoint = std::format(
                                    "http://127.0.0.1:{}/v1/traces", collector_endpoint->port());
                                value.observability.otlp.request_timeout = std::chrono::milliseconds{20};
                                value.observability.otlp.max_attempts = 1;
                            }
                            value.lifecycle.total_stop_timeout = std::chrono::milliseconds{100};
                            app::configured_service database{.name = "mysql", .enabled = true};
                            database.properties = cnetmod::json::object();
                            database.properties["host"] = mysql_test_host;
                            database.properties["port"] = mysql_test_port;
                            database.properties["username"] = std::getenv("CNETMOD_MYSQL_TEST_USER");
                            database.properties["password"] = std::getenv("CNETMOD_MYSQL_TEST_PASSWORD");
                            database.properties["database"] = std::getenv("CNETMOD_MYSQL_TEST_DATABASE");
                            database.properties["minimum_size"] = 1;
                            database.properties["maximum_size"] = 1;
                            value.services.emplace("database", std::move(database));
                        })
                    .enable_auto_configuration()
                    .service(holder)
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
    {
        probe_io->stop();
        return;
    }
    auto database = host->services().require<app::mysql_service>("default");
    if (!database)
        return;
    holder->database = &database->get();
    const auto result = host->run();
    ASSERT_FALSE(result.has_value());
    if (!result)
        ASSERT_EQ(result.error(), bind_error.error());
    ASSERT_TRUE(host->state() == app::application_state::cleanup_failed);
    ASSERT_TRUE(holder->lease.has_value());
    ASSERT_EQ(holder->database->pool().checked_out_count(), 1U);
    holder->lease.reset();
    ASSERT_TRUE(host->retry_cleanup(std::chrono::seconds{1}).has_value());
    ASSERT_TRUE(host->state() == app::application_state::stopped);
    ASSERT_EQ(holder->database->pool().checked_out_count(), 0U);
    ASSERT_EQ(holder->database->pool().waiter_count(), 0U);
    ASSERT_EQ(holder->starts, 1U);
    ASSERT_EQ(holder->stops, 1U);
    ASSERT_TRUE(host->retry_cleanup(std::chrono::seconds{1}).has_value());
    ASSERT_EQ(holder->starts, 1U);
    ASSERT_EQ(holder->stops, 1U);
    const auto statistics = host->telemetry().statistics();
    if (observation_enabled)
    {
        ASSERT_TRUE(statistics.accepted_spans > 0);
        ASSERT_TRUE(statistics.accepted_metrics > 0);
        ASSERT_TRUE(statistics.accepted_logs > 0);
        if (collector_mode == 1)
        {
            ASSERT_TRUE(statistics.failed_batches > 0);
            ASSERT_EQ(statistics.exported, 0U);
        }
        else
            ASSERT_TRUE(statistics.exported > 0);
    }
    else
        ASSERT_EQ(statistics.accepted, 0U);
    if (collector_mode == 2)
    {
        auto close_receiver = [&]() -> cnetmod::task<void>
        {
            receiver.stop();
            receiver.abort_connections();
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while ((!accept.handle().done() || receiver.active_connections() != 0) &&
                std::chrono::steady_clock::now() < limit)
                (void)co_await cnetmod::async_timer_wait(*probe_io, std::chrono::milliseconds{1});
            probe_io->stop();
        };
        auto closing = close_receiver();
        probe_io->post(closing.handle());
        receiver_thread.join();
        ASSERT_TRUE(closing.handle().done());
        ASSERT_TRUE(accept.handle().done());
        ASSERT_EQ(receiver.active_connections(), 0U);
        ASSERT_FALSE(invalid_payload);
        ASSERT_TRUE(mysql_start_metric);
        ASSERT_EQ(mysql_start_spans.size(), 1U);
        ASSERT_EQ(mysql_start_logs.size(), 1U);
        if (mysql_start_spans.size() == 1 && mysql_start_logs.size() == 1)
        {
            ASSERT_FALSE(mysql_start_spans.front().first.empty());
            ASSERT_FALSE(mysql_start_spans.front().second.empty());
            ASSERT_TRUE(mysql_start_spans.front() == mysql_start_logs.front());
        }
        for (const auto count : received)
            ASSERT_TRUE(count > 0);
    }
}

TEST(mysql_live_host_retries_cleanup_after_retained_lease_return)
{
    verify_mysql_host_lease_cleanup(0);
}

TEST(mysql_live_host_collector_failure_preserves_lease_cleanup)
{
    verify_mysql_host_lease_cleanup(1);
}

TEST(mysql_live_host_exports_three_signals_during_lease_rollback)
{
    verify_mysql_host_lease_cleanup(2);
}

int main()
{
    const auto* enabled = std::getenv("CNETMOD_MYSQL_INTEGRATION");
    if (!enabled || std::string_view{enabled} != "1")
        return 77;
    if (const auto* configured = std::getenv("CNETMOD_MYSQL_TEST_PORT"))
    {
        const std::string_view text{configured};
        unsigned port{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
        if (error != std::errc{} || end != text.data() + text.size() || port == 0 || port > 65535)
            return EXIT_FAILURE;
        mysql_test_port = static_cast<std::uint16_t>(port);
    }
    if (const auto* configured = std::getenv("CNETMOD_MYSQL_TEST_HOST"))
    {
        if (*configured == '\0')
            return EXIT_FAILURE;
        mysql_test_host = configured;
    }
    for (const auto* name : {"CNETMOD_MYSQL_TEST_USER", "CNETMOD_MYSQL_TEST_PASSWORD", "CNETMOD_MYSQL_TEST_DATABASE"})
        if (!std::getenv(name))
            return EXIT_FAILURE;
    return cnetmod::test::run_all();
}
