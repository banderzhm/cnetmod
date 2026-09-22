#include "test_framework.hpp"
#include <cnetmod/config.hpp>

import std;
import cnetmod.application.health_registry;
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.application.service_lifecycle;
import cnetmod.application.service_registry;
import cnetmod.application.recovery_policy;
import cnetmod.observability.export_retry;
import cnetmod.observability.export_response;
import cnetmod.json;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.operation_scope;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.dns;
import cnetmod.coro.cancel;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.observability.otlp;
import cnetmod.observability;
import cnetmod.protocol.http.middleware.tracing;
#ifdef CNETMOD_TEST_ORM
import cnetmod.orm.database_session;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_dialect;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import cnetmod.protocol.kafka;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
import cnetmod.protocol.mqtt;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import cnetmod.protocol.amqp091;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import cnetmod.protocol.amqp10;
#endif

namespace {

auto settle_collector(cnetmod::io_context& io, cnetmod::http::server& collector,
    cnetmod::task<void>& accept_loop, cnetmod::observability::otlp_http_exporter& exporter)
    -> cnetmod::task<void>
{
    const auto settled = co_await exporter.shutdown(std::chrono::milliseconds{100},
        std::chrono::milliseconds{100});
    ASSERT_TRUE(settled.has_value());
    collector.stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while ((!accept_loop.handle().done() || collector.active_connections() != 0) &&
        std::chrono::steady_clock::now() < deadline)
        (void)co_await cnetmod::async_timer_wait(io, std::chrono::milliseconds{1});
    ASSERT_TRUE(accept_loop.handle().done());
    ASSERT_EQ(collector.active_connections(), 0U);
}

/**
 * @brief Flushes logger-derived OTLP records before settling the local collector.
 *
 * References are coroutine parameters rather than lambda captures, so their
 * lifetime is tied to the task frame and remains valid until the caller joins
 * the task before destroying the referenced test fixtures.
 */
auto settle_telemetry_collector(cnetmod::io_context& io,
    cnetmod::observability::telemetry_hub& telemetry,
    cnetmod::http::server& collector, cnetmod::task<void>& accept_loop)
    -> cnetmod::task<void>
{
    const auto stopped = co_await telemetry.shutdown(std::chrono::seconds{1},
        std::chrono::milliseconds{100});
    ASSERT_TRUE(stopped.has_value());
    collector.stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while ((!accept_loop.handle().done() || collector.active_connections() != 0) &&
        std::chrono::steady_clock::now() < deadline)
        (void)co_await cnetmod::async_timer_wait(io, std::chrono::milliseconds{1});
    ASSERT_TRUE(accept_loop.handle().done());
    ASSERT_EQ(collector.active_connections(), 0U);
    io.stop();
}

} // namespace

#include "application_health_wire_cases.inc"

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import cnetmod.application.openai;
import cnetmod.protocol.openai;

    #include "openai_metric_wire_cases.inc"
#endif

TEST(telemetry_nonblocking_shutdown_waits_for_queued_worker)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.endpoint = "http://127.0.0.1:1/v1/traces"}};
    ASSERT_TRUE(telemetry.submit_log({.body = "shutdown-test"}));
    ASSERT_FALSE(telemetry.try_settle_shutdown());
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (!telemetry.try_settle_shutdown() && std::chrono::steady_clock::now() < limit)
        (void)io->poll();
    ASSERT_TRUE(telemetry.try_settle_shutdown());
    ASSERT_TRUE(telemetry.try_settle_shutdown());
    ASSERT_EQ(telemetry.statistics().dropped_logs, 1U);
    ASSERT_FALSE(telemetry.submit_log({.body = "closed"}));
    ASSERT_EQ(telemetry.statistics().dropped_logs, 2U);
}

TEST(telemetry_framework_log_capture_is_explicit_and_removable)
{
    auto io = cnetmod::make_io_context();
    logger::init("test", logger::level::info);
    logger::set_console_enabled(false);
    {
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.endpoint = "http://127.0.0.1:1/v1/logs",
                .export_traces = false,
                .export_metrics = false,
                .export_logs = true}};
        logger::info("not-captured-by-default");
        logger::flush();
        ASSERT_EQ(telemetry.statistics().accepted_logs, 0U);
        telemetry.close();
    }
    {
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.endpoint = "http://127.0.0.1:1/v1/logs",
                .export_traces = false,
                .export_metrics = false,
                .export_logs = true,
                .capture_framework_logs = true}};
        logger::info("captured-by-explicit-bridge");
        logger::flush();
        ASSERT_EQ(telemetry.statistics().accepted_logs, 1U);
        telemetry.close();
        logger::info("not-captured-after-close");
        logger::flush();
        ASSERT_EQ(telemetry.statistics().accepted_logs, 1U);
    }
    logger::shutdown();
}

TEST(http_stop_completes_pending_accept_before_loop_destruction)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::server server{*io};
    ASSERT_TRUE(server.listen("127.0.0.1", 0).has_value());
    auto accept_loop = server.run();
    accept_loop.handle().resume();
    ASSERT_FALSE(accept_loop.handle().done());
    server.stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (!accept_loop.handle().done() && std::chrono::steady_clock::now() < deadline)
        (void)io->poll();
    ASSERT_TRUE(accept_loop.handle().done());
    server.stop();
    (void)io->poll();
}

TEST(http_abort_settles_idle_connection_without_destroying_pending_io)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    reservation->close();
    cnetmod::http::server server{*io};
    ASSERT_TRUE(server.listen("127.0.0.1", endpoint->port()).has_value());
    auto accept_loop = server.run();
    accept_loop.handle().resume();
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*io, *peer, *endpoint)).has_value());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (server.active_connections() == 0 && std::chrono::steady_clock::now() < deadline)
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_EQ(server.active_connections(), 1U);
        server.stop();
        server.abort_connections();
        server.abort_connections();
        while ((!accept_loop.handle().done() || server.active_connections() != 0) &&
            std::chrono::steady_clock::now() < deadline)
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(accept_loop.handle().done());
        ASSERT_EQ(server.active_connections(), 0U);
        io->stop();
    };
    cnetmod::spawn(*io, exercise());
    io->run();
}

TEST(export_retry_delay_saturates_before_integer_conversion_or_multiplication)
{
    using namespace std::chrono_literals;
    const auto delay = [](auto initial, auto maximum, std::size_t attempt, std::string_view header = {})
    {
        return cnetmod::observability::detail::export_retry_delay(initial, maximum, attempt, header);
    };
    ASSERT_TRUE(delay(100ms, 5000ms, 0) == 100ms);
    ASSERT_TRUE(delay(100ms, 5000ms, 3) == 800ms);
    ASSERT_TRUE(delay(100ms, 5000ms, std::numeric_limits<std::size_t>::max()) == 5000ms);
    ASSERT_TRUE(delay(0ms, 5000ms, std::numeric_limits<std::size_t>::max()) == 0ms);
    ASSERT_TRUE(delay(-1ms, -5ms, 0) == 0ms);
    ASSERT_TRUE(delay(100ms, 5000ms, 0, "2") == 2000ms);
    ASSERT_TRUE(delay(100ms, 5000ms, 0, "0") == 0ms);
    ASSERT_TRUE(delay(100ms, 5000ms, 0, "18446744073709551615") == 5000ms);
    ASSERT_TRUE(delay(100ms, 5000ms, 0, "99999999999999999999999999999") == 5000ms);
    ASSERT_TRUE(delay(100ms, 5000ms, 1, "99999999999999999999999999999x") == 200ms);
    ASSERT_TRUE(delay(100ms, 5000ms, 1, "-1") == 200ms);
    const auto maximum = std::chrono::milliseconds::max();
    ASSERT_TRUE(delay(maximum / 2 + 1ms, maximum, 1) == maximum);
    ASSERT_TRUE(delay(maximum / 2, maximum, 1) == maximum - 1ms);
}

TEST(disabled_telemetry_has_no_span_sink_or_export_queue)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*context};
    ASSERT_FALSE(static_cast<bool>(telemetry.spans()));
    ASSERT_FALSE(telemetry.submit_metric({.name = "disabled"}));
    ASSERT_FALSE(telemetry.submit_log({.body = "disabled"}));
    ASSERT_EQ(telemetry.statistics().accepted, std::uint64_t{0});
    ASSERT_EQ(telemetry.statistics().dropped, std::uint64_t{0});
}

TEST(lazy_signal_factories_are_skipped_when_disabled_or_closed)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub disabled{*context};
    unsigned factories = 0;
    auto metric = [&]
    {
        ++factories;
        return cnetmod::observability::otel_metric_record{.name = "lazy"};
    };
    auto log = [&]
    {
        ++factories;
        return cnetmod::observability::otel_log_record{.body = "lazy"};
    };
    ASSERT_FALSE(disabled.submit_metric_lazy(metric));
    ASSERT_FALSE(disabled.submit_log_lazy(log));
    ASSERT_EQ(factories, 0U);
    cnetmod::observability::telemetry_hub enabled{*context,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
    ASSERT_TRUE(enabled.submit_metric_lazy(metric));
    ASSERT_FALSE(enabled.submit_log_lazy(log));
    ASSERT_EQ(factories, 1U);
    ASSERT_FALSE(enabled.submit_metric_lazy([]()
                                                -> cnetmod::observability::otel_metric_record
        {
            throw std::runtime_error("factory failure");
        }));
    enabled.close();
    ASSERT_FALSE(enabled.submit_metric_lazy(metric));
    ASSERT_EQ(factories, 1U);
}

TEST(otlp_invalid_metrics_are_rejected_before_consuming_queue_capacity)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::otlp_http_exporter exporter{*io,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics", .queue_capacity = 2U}};
    for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity()})
        ASSERT_FALSE(exporter.submit(cnetmod::observability::otel_metric_record{
            .name = "invalid",
            .value = invalid}));
    ASSERT_FALSE(exporter.submit(cnetmod::observability::otel_metric_record{
        .name = "counter",
        .value = -1.0,
        .kind = cnetmod::observability::otel_metric_kind::counter}));
    ASSERT_FALSE(exporter.submit(cnetmod::observability::otel_metric_record{}));
    ASSERT_FALSE(exporter.submit(cnetmod::observability::otel_metric_record{
        .name = "invalid.histogram",
        .kind = cnetmod::observability::otel_metric_kind::histogram,
        .explicit_bounds = {2, 1}}));
    ASSERT_FALSE(exporter.submit(cnetmod::observability::otel_metric_record{
        .name = "invalid.kind",
        .kind = static_cast<cnetmod::observability::otel_metric_kind>(99)}));
    ASSERT_TRUE(exporter.submit(cnetmod::observability::otel_metric_record{
        .name = "negative.gauge",
        .value = -1.0}));
    ASSERT_TRUE(exporter.submit(cnetmod::observability::otel_metric_record{
        .name = "tiny.gauge",
        .value = 1e-12}));
    ASSERT_EQ(exporter.statistics().accepted_metrics, std::uint64_t{2});
    ASSERT_EQ(exporter.statistics().dropped_metrics, std::uint64_t{7});
    exporter.close();
}

TEST(otlp_metrics_only_accepts_metrics_without_a_trace_endpoint)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*context,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
    ASSERT_FALSE(static_cast<bool>(telemetry.spans()));
    ASSERT_TRUE(telemetry.submit_metric({.name = "metrics.only", .value = 1.0}));
    ASSERT_FALSE(telemetry.submit_log({.body = "disabled"}));
    ASSERT_EQ(telemetry.statistics().accepted_metrics, std::uint64_t{1});
    ASSERT_EQ(telemetry.statistics().accepted_logs, std::uint64_t{0});
    ASSERT_EQ(telemetry.statistics().dropped, std::uint64_t{0});
    telemetry.close();
}

TEST(otlp_explicit_signal_switches_override_inherited_endpoints)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*context,
        {.endpoint = "http://127.0.0.1:1/v1/traces",
            .export_traces = false,
            .export_metrics = false,
            .export_logs = true}};
    ASSERT_FALSE(static_cast<bool>(telemetry.spans()));
    ASSERT_FALSE(telemetry.submit_metric({.name = "disabled"}));
    ASSERT_TRUE(telemetry.submit_log({.body = "logs only"}));
    ASSERT_EQ(telemetry.statistics().accepted_logs, std::uint64_t{1});
    ASSERT_EQ(telemetry.statistics().dropped, std::uint64_t{0});
    telemetry.close();
}

TEST(otlp_all_signals_disabled_overrides_configured_endpoints)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*context,
        {.endpoint = "http://127.0.0.1:1/v1/traces",
            .export_traces = false,
            .export_metrics = false,
            .export_logs = false}};
    ASSERT_FALSE(static_cast<bool>(telemetry.spans()));
    ASSERT_FALSE(telemetry.submit_metric({.name = "disabled"}));
    ASSERT_FALSE(telemetry.submit_log({.body = "disabled"}));
    ASSERT_EQ(telemetry.statistics().accepted, std::uint64_t{0});
    ASSERT_EQ(telemetry.statistics().dropped, std::uint64_t{0});
}

TEST(otlp_exporter_uses_bounded_nonblocking_submission)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::otlp_http_exporter exporter{*context,
        {.endpoint = "http://127.0.0.1:1/v1/traces", .queue_capacity = 2U, .max_batch_size = 1U}};
    const auto trace = cnetmod::http::tracing::new_root_context();
    cnetmod::http::tracing::completed_span span{
        .context = trace,
        .method = "GET",
        .path = "/health",
        .status_code = 200,
    };
    ASSERT_TRUE(exporter.submit(span));
    ASSERT_TRUE(exporter.submit(span));
    ASSERT_FALSE(exporter.submit(span));
    const auto stats = exporter.statistics();
    ASSERT_EQ(stats.accepted, std::uint64_t{2});
    ASSERT_EQ(stats.dropped, std::uint64_t{1});
    exporter.close();
    ASSERT_FALSE(exporter.submit(std::move(span)));
}

TEST(telemetry_hub_composes_protocol_adapters_without_global_state)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*context,
        {.endpoint = "http://127.0.0.1:1/v1/traces",
            .service_name = "composition-test",
            .queue_capacity = 4U,
            .max_attempts = 1U}};
    telemetry.metrics().counter_add("composition_operations_total");
    ASSERT_TRUE(telemetry.metrics().render_openmetrics().contains(
        "composition_operations_total"));

    auto server_options = telemetry.server_tracing();
    ASSERT_TRUE(static_cast<bool>(server_options.on_end));
    server_options.on_end({
        .context = cnetmod::http::tracing::new_root_context(),
        .name = "composition",
    });
    ASSERT_EQ(telemetry.statistics().accepted, std::uint64_t{1});
    telemetry.close();
}

TEST(telemetry_hub_sampling_ratio_updates_without_restart)
{
    auto context = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*context,
        {.endpoint = "http://127.0.0.1:1/v1/traces"}};
    telemetry.set_sampling_ratio(0.0);
    auto exporter = telemetry.spans();
    auto rejected = cnetmod::instrumentation::operation_scope::start(exporter, []
        {
            return cnetmod::instrumentation::start_client_span({}, "not-sampled");
        });
    ASSERT_TRUE(rejected.context() != nullptr);
    ASSERT_EQ(rejected.context()->flags & 1U, 0U);
    telemetry.set_sampling_ratio(1.0);
    rejected.complete();
    ASSERT_EQ(telemetry.statistics().accepted, std::uint64_t{0});
    telemetry.set_sampling_ratio(1.0);
    auto accepted = cnetmod::instrumentation::operation_scope::start(exporter, []
        {
            return cnetmod::instrumentation::start_client_span({}, "sampled");
        });
    ASSERT_TRUE(accepted.context() != nullptr);
    ASSERT_EQ(accepted.context()->flags & 1U, 1U);
    telemetry.set_sampling_ratio(0.0);
    accepted.complete();
    ASSERT_EQ(telemetry.statistics().accepted, std::uint64_t{1});
    telemetry.close();
}

TEST(observability_messaging_propagates_w3c_context)
{
    const auto trace = cnetmod::http::tracing::new_root_context();
    auto previous = cnetmod::http::tracing::new_root_context();
    previous.tracestate = "vendor=previous";
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
    cnetmod::kafka::record produced;
    cnetmod::observability::messaging::inject(produced, previous);
    cnetmod::observability::messaging::inject(produced, trace);
    cnetmod::kafka::consumed_record consumed;
    consumed.headers = produced.headers;
    const auto kafka = cnetmod::observability::messaging::extract(consumed);
    ASSERT_TRUE(kafka.has_value());
    ASSERT_EQ(kafka->trace_id, trace.trace_id);
    ASSERT_TRUE(kafka->tracestate.empty());
    ASSERT_EQ(produced.headers.size(), std::size_t{1});
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
    cnetmod::mqtt::properties properties;
    cnetmod::observability::messaging::inject(properties, previous);
    cnetmod::observability::messaging::inject(properties, trace);
    const auto mqtt = cnetmod::observability::messaging::extract(properties);
    ASSERT_TRUE(mqtt.has_value());
    ASSERT_EQ(mqtt->trace_id, trace.trace_id);
    ASSERT_TRUE(mqtt->tracestate.empty());
    ASSERT_EQ(properties.size(), std::size_t{1});
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    cnetmod::amqp091::message message091;
    cnetmod::observability::messaging::inject(message091, previous);
    cnetmod::observability::messaging::inject(message091, trace);
    const auto amqp091 =
        cnetmod::observability::messaging::extract(message091);
    ASSERT_TRUE(amqp091.has_value());
    ASSERT_EQ(amqp091->trace_id, trace.trace_id);
    ASSERT_TRUE(amqp091->tracestate.empty());
    ASSERT_EQ(message091.headers.count("tracestate"), std::size_t{0});
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
    cnetmod::amqp10::message message10;
    cnetmod::observability::messaging::inject(message10, previous);
    cnetmod::observability::messaging::inject(message10, trace);
    const auto amqp10 = cnetmod::observability::messaging::extract(message10);
    ASSERT_TRUE(amqp10.has_value());
    ASSERT_EQ(amqp10->trace_id, trace.trace_id);
    ASSERT_TRUE(amqp10->tracestate.empty());
    ASSERT_EQ(message10.application.count("tracestate"), std::size_t{0});
#endif
}

namespace {

#ifdef CNETMOD_TEST_ORM
struct rejected_sql_client
{
    auto query(std::string_view) -> cnetmod::task<cnetmod::orm::query_result>
    {
        co_return cnetmod::orm::query_result{
            .error_msg = "private database diagnostic",
            .sql_state = "42S22",
            .error_code = 1054};
    }

    auto execute(std::string_view sql) -> cnetmod::task<cnetmod::orm::query_result>
    {
        return query(sql);
    }

    auto execute(cnetmod::orm::parameterized_query) -> cnetmod::task<cnetmod::orm::query_result>
    {
        return query("");
    }
};
#endif

struct collector_observation
{
    std::size_t requests{};
    std::string content_type;
    std::string payload;
    std::string metrics_payload;
    std::vector<std::string> metric_batches;
    std::string logs_payload;
    bool flush_succeeded{};
    std::string authorization;
    std::uint64_t retries{};
    std::string expected_parent_span_id;
};

auto export_to_local_collector(cnetmod::io_context& context,
    cnetmod::http::server& collector, collector_observation& observation,
    std::uint16_t port, cnetmod::task<void>& accept_loop) -> cnetmod::task<void>
{
    cnetmod::observability::otlp_http_exporter exporter{context,
        {.endpoint = "http://127.0.0.1:" + std::to_string(port) + "/v1/traces",
            .service_name = "otlp-wire-e2e",
            .service_version = "2.0.0",
            .service_namespace = "tests",
            .service_instance_id = "collector-fixture",
            .deployment_environment = "integration",
            .resource_attributes = {{"service.owner", "cnetmod"}},
            .headers = {{"Authorization", "Bearer test-token"}},
            .queue_capacity = 8U,
            .max_batch_size = 8U,
            .request_timeout = std::chrono::seconds{2},
            .max_attempts = 2U,
            .initial_retry_delay = std::chrono::milliseconds{1},
            .max_retry_delay = std::chrono::milliseconds{1},
            .max_metric_attribute_sets = 1U}};
    const auto trace = cnetmod::http::tracing::new_root_context();
    const cnetmod::http::tracing::completed_span first{
        .context = trace,
        .method = "POST",
        .path = "/orders/42",
        .status_code = 201,
        .elapsed = std::chrono::milliseconds{3},
    };
    auto child = cnetmod::http::tracing::child_context(trace);
    observation.expected_parent_span_id = trace.span_id;
    const cnetmod::http::tracing::completed_span second{
        .context = std::move(child),
        .method = "GET",
        .path = "/orders/42",
        .status_code = 200,
        .elapsed = std::chrono::milliseconds{1},
        .has_remote_parent = true,
        .parent_span_id = trace.span_id,
        .started_at = std::chrono::system_clock::now() -
            std::chrono::milliseconds{2},
        .ended_at = std::chrono::system_clock::now(),
        .kind = cnetmod::http::tracing::span_kind::server,
    };
    const cnetmod::http::tracing::completed_span redis{
        .context = cnetmod::http::tracing::child_context(trace),
        .name = "REDIS GET",
        .elapsed = std::chrono::milliseconds{1},
        .failed = true,
        .attributes = {{"db.system", "redis"}, {"db.operation", "GET"}},
    };
    const auto metric_submitted = exporter.submit(
        cnetmod::observability::otel_metric_record{
            .name = "application.requests",
            .value = 3.0,
            .kind = cnetmod::observability::otel_metric_kind::counter,
            .unit = "{request}",
        });
    const auto log_submitted = exporter.submit(
        cnetmod::observability::otel_log_record{
            .severity = "WARN",
            .body = "dependency recovering",
            .trace_id = trace.trace_id,
            .span_id = trace.span_id,
        });
    bool precision_submitted = true;
    std::size_t precision_index{};
    for (const double value : {1e-12, -1.2345678901234567, 1.7976931348623157e308})
        precision_submitted = exporter.submit(cnetmod::observability::otel_metric_record{
                                  .name = "precision." + std::to_string(precision_index++),
                                  .value = value}) &&
            precision_submitted;
    bool outcomes_submitted = true;
    for (const auto status : {cnetmod::instrumentation::operation_status::cancelled,
             cnetmod::instrumentation::operation_status::timeout,
             cnetmod::instrumentation::operation_status::abandoned})
    {
        outcomes_submitted = exporter.submit(cnetmod::http::tracing::completed_span{
                                 .context = cnetmod::http::tracing::child_context(trace),
                                 .name = "terminal-outcome",
                                 .method = "GET",
                                 .result = {status, std::make_error_code(status == cnetmod::instrumentation::operation_status::timeout ? std::errc::timed_out : std::errc::operation_canceled)},
                             }) &&
            outcomes_submitted;
    }
    bool sql_submitted = true;
#ifdef CNETMOD_TEST_ORM
    rejected_sql_client sql_client;
    for (const auto dialect : {cnetmod::orm::sql_dialect::mysql, cnetmod::orm::sql_dialect::postgresql})
    {
        cnetmod::orm::database_session session{sql_client, dialect};
        const auto result = co_await session.query("private query text", trace,
            [&](const auto& span)
            {
                sql_submitted = exporter.submit(span) && sql_submitted;
            });
        ASSERT_TRUE(result.is_err());
        ASSERT_EQ(result.error_msg, "private database diagnostic");
    }
#endif
    if (sql_submitted && exporter.submit(first) && exporter.submit(second) &&
        exporter.submit(redis) && metric_submitted && log_submitted && outcomes_submitted && precision_submitted)
    {
        const auto flushed = co_await exporter.flush(std::chrono::seconds{2});
        observation.flush_succeeded = flushed.has_value();
        const bool more_metrics = exporter.submit(cnetmod::observability::otel_metric_record{
                                      .name = "application.requests",
                                      .value = 2,
                                      .kind = cnetmod::observability::otel_metric_kind::counter,
                                      .unit = "{request}"}) &&
            exporter.submit(cnetmod::observability::otel_metric_record{
                .name = "application.requests",
                .value = 7,
                .kind = cnetmod::observability::otel_metric_kind::counter,
                .unit = "{request}",
                .attributes = {{"tenant", "overflow-tenant"}}});
        bool histograms_submitted = true;
        for (double value : {0.5, 1.0, 2.0})
            histograms_submitted = exporter.submit(cnetmod::observability::otel_metric_record{
                                       .name = "duration",
                                       .value = value,
                                       .kind = cnetmod::observability::otel_metric_kind::histogram,
                                       .unit = "s",
                                       .explicit_bounds = {0.5, 1}}) &&
                histograms_submitted;
        histograms_submitted = exporter.submit(cnetmod::observability::otel_metric_record{
                                   .name = "temperature",
                                   .value = -5,
                                   .kind = cnetmod::observability::otel_metric_kind::histogram}) &&
            histograms_submitted;
        const auto second_flush = co_await exporter.flush(std::chrono::seconds{2});
        observation.flush_succeeded = observation.flush_succeeded && more_metrics && histograms_submitted && second_flush.has_value();
        observation.retries = exporter.statistics().retries;
    }
    co_await settle_collector(context, collector, accept_loop, exporter);
    context.stop();
}

} // namespace

TEST(otlp_exporter_posts_valid_otlp_json_to_a_real_http_collector)
{
    constexpr std::uint16_t collector_port = 19431;
    auto context = cnetmod::make_io_context();
    cnetmod::net_init network;
    collector_observation observation;
    cnetmod::http::router routes;
    routes.any("/*path", [&observation](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            observation.content_type = request.get_header("content-type");
            observation.authorization = request.get_header("authorization");
            auto payload = std::string{co_await request.read_full_body()};
            if (request.path() == "/v1/metrics")
            {
                observation.metric_batches.push_back(payload);
                observation.metrics_payload = std::move(payload);
            }
            else if (request.path() == "/v1/logs")
                observation.logs_payload = std::move(payload);
            else
                observation.payload = std::move(payload);
            ++observation.requests;
            if (observation.requests == 1U)
            {
                request.resp().set_header("Retry-After", "18446744073709551615");
                request.text(cnetmod::http::status::service_unavailable,
                    "retry");
            }
            else
                request.json(cnetmod::http::status::ok, "{}");
            co_return;
        });
    cnetmod::http::server collector{*context};
    collector.set_router(std::move(routes));
    ASSERT_TRUE(collector.listen("127.0.0.1", collector_port).has_value());

    auto accept_loop = collector.run();
    accept_loop.handle().resume();
    cnetmod::spawn(*context, export_to_local_collector(*context, collector, observation, collector_port, accept_loop));
    context->run();

    ASSERT_TRUE(observation.flush_succeeded);
    ASSERT_EQ(observation.requests, std::size_t{5});
    ASSERT_EQ(observation.retries, std::uint64_t{1});
    ASSERT_EQ(observation.content_type, "application/json");
    ASSERT_EQ(observation.authorization, "Bearer test-token");
    ASSERT_TRUE(observation.payload.contains("\"resourceSpans\""));
    ASSERT_TRUE(observation.payload.contains("\"service.name\""));
    ASSERT_TRUE(observation.payload.contains("otlp-wire-e2e"));
    ASSERT_TRUE(observation.payload.contains("service.version"));
    ASSERT_TRUE(observation.payload.contains("deployment.environment.name"));
    ASSERT_TRUE(observation.payload.contains("service.owner"));
    ASSERT_TRUE(observation.payload.contains("POST /orders/42"));
    ASSERT_TRUE(observation.payload.contains("GET /orders/42"));
    ASSERT_TRUE(observation.payload.contains("REDIS GET"));
    ASSERT_TRUE(observation.payload.contains("db.system"));
    ASSERT_TRUE(observation.payload.contains("STATUS_CODE_ERROR"));
    ASSERT_TRUE(observation.payload.contains("\"traceId\""));
    ASSERT_TRUE(observation.payload.contains("\"parentSpanId\""));
    ASSERT_TRUE(observation.payload.contains(
        observation.expected_parent_span_id));
    ASSERT_TRUE(observation.metrics_payload.contains("\"resourceMetrics\""));
    ASSERT_TRUE(observation.metrics_payload.contains("application.requests"));
    ASSERT_TRUE(observation.logs_payload.contains("\"resourceLogs\""));
    ASSERT_TRUE(observation.logs_payload.contains("dependency recovering"));
    ASSERT_TRUE(observation.logs_payload.contains("\"traceId\""));
    const auto document = cnetmod::json::document::parse(observation.payload);
#ifdef CNETMOD_TEST_ORM
    unsigned database_spans = 0;
    for (const auto& span : document.at("resourceSpans").at(0).at("scopeSpans").at(0).at("spans"))
    {
        if (span.at("name") != "SQL QUERY")
            continue;
        ++database_spans;
        ASSERT_EQ(span.at("kind").get<std::string>(), "SPAN_KIND_CLIENT");
        ASSERT_EQ(span.at("parentSpanId").get<std::string>(), observation.expected_parent_span_id);
        ASSERT_EQ(span.at("status").at("code").get<std::string>(), "STATUS_CODE_ERROR");
        std::map<std::string, std::string> attributes;
        for (const auto& attribute : span.at("attributes"))
        {
            const auto key = attribute.at("key").get<std::string>();
            const auto value = attribute.at("value").at("stringValue").get<std::string>();
            ASSERT_TRUE(attributes.emplace(key, value).second);
            ASSERT_FALSE(value.contains("private"));
            ASSERT_NE(key, "db.query.text");
        }
        const auto system = attributes.at("db.system.name");
        ASSERT_TRUE(system == "mysql" || system == "postgresql");
        const auto expected_code = system == "mysql" ? "1054" : "42S22";
        ASSERT_EQ(attributes.at("db.response.status_code"), expected_code);
        ASSERT_EQ(attributes.at("error.type"), expected_code);
    }
    ASSERT_EQ(database_spans, 2U);
#endif
    unsigned terminal_spans = 0;
    for (const auto& span : document.at("resourceSpans").at(0).at("scopeSpans").at(0).at("spans"))
    {
        if (span.at("name") != "terminal-outcome")
            continue;
        ++terminal_spans;
        std::string outcome;
        bool has_error_code = false;
        for (const auto& attribute : span.at("attributes"))
        {
            ASSERT_FALSE(attribute.at("key") == "http.response.status_code");
            if (attribute.at("key") == "cnetmod.operation.status")
                outcome = attribute.at("value").at("stringValue").get<std::string>();
            if (attribute.at("key") == "cnetmod.error.code")
                has_error_code = true;
        }
        ASSERT_TRUE(has_error_code);
        if (outcome == "cancelled")
            ASSERT_FALSE(span.contains("status"));
        else
        {
            ASSERT_TRUE(outcome == "timeout" || outcome == "abandoned");
            ASSERT_EQ(span.at("status").at("code").get<std::string>(), "STATUS_CODE_ERROR");
        }
    }
    ASSERT_EQ(terminal_spans, 3U);
    const auto metric_document = cnetmod::json::document::parse(observation.metrics_payload);
    ASSERT_EQ(observation.metric_batches.size(), 2U);
    const auto first_metrics = cnetmod::json::document::parse(observation.metric_batches[0]);
    const auto& initial_point = first_metrics.at("resourceMetrics").at(0).at("scopeMetrics").at(0).at("metrics").at(0).at("sum").at("dataPoints").at(0);
    const auto& final_sum = metric_document.at("resourceMetrics").at(0).at("scopeMetrics").at(0).at("metrics").at(0).at("sum");
    ASSERT_TRUE(final_sum.at("aggregationTemporality") == "AGGREGATION_TEMPORALITY_CUMULATIVE");
    ASSERT_EQ(final_sum.at("dataPoints").size(), 2U);
    const auto& final_point = final_sum.at("dataPoints").at(0);
    ASSERT_EQ(initial_point.at("asDouble").get<double>(), 3.0);
    ASSERT_EQ(final_point.at("asDouble").get<double>(), 5.0);
    ASSERT_TRUE(initial_point.at("startTimeUnixNano") == final_point.at("startTimeUnixNano"));
    const auto& overflow = final_sum.at("dataPoints").at(1);
    ASSERT_EQ(overflow.at("asDouble").get<double>(), 7.0);
    ASSERT_TRUE(overflow.at("attributes").at(0).at("key") == "otel.metric.overflow");
    ASSERT_TRUE(overflow.at("attributes").at(0).at("value").at("boolValue").get<bool>());
    std::vector<double> values;
    unsigned histograms{};
    for (const auto& metric : metric_document.at("resourceMetrics").at(0).at("scopeMetrics").at(0).at("metrics"))
    {
        if (metric.contains("gauge"))
            values.push_back(metric.at("gauge").at("dataPoints").at(0).at("asDouble").get<double>());
        if (metric.contains("histogram"))
        {
            ++histograms;
            const auto& histogram = metric.at("histogram");
            ASSERT_TRUE(histogram.at("aggregationTemporality") == "AGGREGATION_TEMPORALITY_CUMULATIVE");
            const auto& point = histogram.at("dataPoints").at(0);
            ASSERT_TRUE(point.contains("startTimeUnixNano"));
            ASSERT_FALSE(point.contains("asDouble"));
            if (metric.at("name") == "duration")
            {
                ASSERT_TRUE(point.at("count") == "3");
                ASSERT_EQ(point.at("sum").get<double>(), 3.5);
                ASSERT_EQ(point.at("min").get<double>(), 0.5);
                ASSERT_EQ(point.at("max").get<double>(), 2.0);
                ASSERT_TRUE(point.at("explicitBounds").get<std::vector<double>>() == std::vector<double>({0.5, 1.0}));
                ASSERT_TRUE(point.at("bucketCounts").get<std::vector<std::string>>() == std::vector<std::string>({"1", "1", "1"}));
            }
            else
            {
                ASSERT_TRUE(metric.at("name") == "temperature");
                ASSERT_TRUE(point.at("count") == "1");
                ASSERT_FALSE(point.contains("sum"));
                ASSERT_EQ(point.at("min").get<double>(), -5.0);
                ASSERT_TRUE(point.at("explicitBounds").empty());
                ASSERT_TRUE(point.at("bucketCounts").get<std::vector<std::string>>() == std::vector<std::string>({"1"}));
            }
        }
    }
    ASSERT_EQ(histograms, 2U);
    ASSERT_TRUE(values == std::vector<double>({1e-12, -1.2345678901234567, 1.7976931348623157e308}));
}

TEST(telemetry_logger_bridge_exports_explicit_correlation_to_real_collector)
{
    constexpr std::uint16_t collector_port = 19438;
    constexpr std::string_view trace_id{"0123456789abcdef0123456789abcdef"};
    constexpr std::string_view span_id{"0123456789abcdef"};
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    std::string logs_payload;
    cnetmod::http::router routes;
    routes.post("/v1/logs", [&logs_payload](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            logs_payload = std::string{co_await request.read_full_body()};
            request.json(cnetmod::http::status::ok, "{}");
            co_return;
        });
    cnetmod::http::server collector{*io};
    collector.set_router(std::move(routes));
    ASSERT_TRUE(collector.listen("127.0.0.1", collector_port).has_value());
    auto accept_loop = collector.run();
    accept_loop.handle().resume();

    logger::init("test", logger::level::info);
    logger::set_console_enabled(false);
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.logs_endpoint = "http://127.0.0.1:" + std::to_string(collector_port) + "/v1/logs",
            .request_timeout = std::chrono::seconds{1},
            .max_attempts = 1U,
            .export_traces = false,
            .export_metrics = false,
            .export_logs = true,
            .capture_framework_logs = true}};
    logger::log(logger::level::info, {.trace_id = trace_id, .span_id = span_id},
        "persisted order");
    logger::flush();

    cnetmod::spawn(*io, settle_telemetry_collector(*io, telemetry, collector, accept_loop));
    io->run();
    logger::shutdown();

    const auto document = cnetmod::json::document::parse(logs_payload);
    const auto& record = document.at("resourceLogs").at(0).at("scopeLogs").at(0).at("logRecords").at(0);
    ASSERT_EQ(record.at("body").at("stringValue").get<std::string>(), "persisted order");
    ASSERT_EQ(record.at("severityText").get<std::string>(), "INFO");
    ASSERT_EQ(record.at("traceId").get<std::string>(), trace_id);
    ASSERT_EQ(record.at("spanId").get<std::string>(), span_id);
}

TEST(permanent_collector_failure_exhausts_retry_budget_without_stranding_flush)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    unsigned requests{};
    bool drained{};
    cnetmod::observability::otlp_exporter_statistics statistics;
    cnetmod::http::router routes;
    routes.any("/*path", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            (void)co_await request.read_full_body();
            ++requests;
            request.resp().set_header("Retry-After", "999999999999999999999999999999");
            request.text(cnetmod::http::status::service_unavailable, "unavailable");
            co_return;
        });
    cnetmod::http::server collector{*io};
    collector.set_router(std::move(routes));
    ASSERT_TRUE(collector.listen("127.0.0.1", 19433).has_value());
    auto accept_loop = collector.run();
    accept_loop.handle().resume();
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::observability::otlp_http_exporter exporter{*io,
            {.metrics_endpoint = "http://127.0.0.1:19433/v1/metrics",
                .request_timeout = std::chrono::seconds{1},
                .max_attempts = 2U,
                .initial_retry_delay = std::chrono::milliseconds{1},
                .max_retry_delay = std::chrono::milliseconds{2}}};
        const bool accepted = exporter.submit(cnetmod::observability::otel_metric_record{
            .name = "failure.test",
            .value = 1});
        const auto result = co_await exporter.flush(std::chrono::seconds{2});
        drained = accepted && result.has_value();
        statistics = exporter.statistics();
        co_await settle_collector(*io, collector, accept_loop, exporter);
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(drained);
    ASSERT_EQ(requests, 2U);
    ASSERT_EQ(statistics.retries, 1U);
    ASSERT_EQ(statistics.failed_batches, 1U);
    ASSERT_EQ(statistics.exported, 0U);
}

TEST(collector_connection_refusal_recovers_and_accepts_later_batches)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    if (!reservation)
        return;
    ASSERT_TRUE(reservation->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    cnetmod::observability::otlp_http_exporter exporter{*io,
        {.endpoint = std::format("http://127.0.0.1:{}/v1/traces", endpoint->port()),
            .request_timeout = std::chrono::milliseconds{200},
            .max_attempts = 4U,
            .initial_retry_delay = std::chrono::milliseconds{100},
            .max_retry_delay = std::chrono::milliseconds{100}}};
    std::array<unsigned, 3> requests{};
    bool recovered{};
    bool drained{};
    cnetmod::http::router routes;
    routes.post("/v1/*signal", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            const auto body = std::string{co_await request.read_full_body()};
            const auto document = cnetmod::json::document::parse(body);
            if (request.path() == "/v1/traces")
            {
                ASSERT_TRUE(document.contains("resourceSpans"));
                const auto& spans = document.at("resourceSpans").at(0).at("scopeSpans").at(0).at("spans");
                ASSERT_EQ(spans.size(), 1U);
                ASSERT_EQ(spans.at(0).at("name").get<std::string>(), requests[0] == 0 ? "before recovery" : "after recovery");
                ++requests[0];
            }
            else if (request.path() == "/v1/metrics")
            {
                ASSERT_TRUE(document.contains("resourceMetrics"));
                const auto& metrics = document.at("resourceMetrics").at(0).at("scopeMetrics").at(0).at("metrics");
                ASSERT_EQ(metrics.size(), 1U);
                const auto& points = metrics.at(0).at("gauge").at("dataPoints");
                ASSERT_EQ(points.size(), 1U);
                ASSERT_EQ(points.at(0).at("asDouble").get<double>(), requests[1] == 0 ? 1.0 : 2.0);
                ++requests[1];
            }
            else
            {
                ASSERT_EQ(request.path(), "/v1/logs");
                ASSERT_TRUE(document.contains("resourceLogs"));
                const auto& logs = document.at("resourceLogs").at(0).at("scopeLogs").at(0).at("logRecords");
                ASSERT_EQ(logs.size(), 1U);
                ASSERT_EQ(logs.at(0).at("body").at("stringValue").get<std::string>(), requests[2] == 0 ? "before recovery" : "after recovery");
                ++requests[2];
            }
            request.json(cnetmod::http::status::ok, "{}");
        });
    cnetmod::http::server collector{*io};
    collector.set_router(std::move(routes));
    auto recover = [&]() -> cnetmod::task<void>
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (exporter.statistics().retries == 0 && std::chrono::steady_clock::now() < until)
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(exporter.statistics().retries > 0);
        reservation->close();
        recovered = collector.listen("127.0.0.1", endpoint->port()).has_value();
        if (recovered)
            co_await collector.run();
    };
    auto recovery_loop = recover();
    recovery_loop.handle().resume();
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto submit_signals = [&](std::string label, double value)
        {
            const bool trace = exporter.submit(cnetmod::http::tracing::completed_span{
                .context = cnetmod::http::tracing::new_root_context(),
                .name = label});
            const bool metric = exporter.submit(cnetmod::observability::otel_metric_record{
                .name = "recovery.gauge",
                .value = value});
            const bool log = exporter.submit(cnetmod::observability::otel_log_record{.body = std::move(label)});
            return trace && metric && log;
        };
        const bool first = submit_signals("before recovery", 1.0);
        const auto flushed = co_await exporter.flush(std::chrono::seconds{3});
        const bool second = submit_signals("after recovery", 2.0);
        const auto flushed_again = co_await exporter.flush(std::chrono::seconds{3});
        drained = first && second && flushed.has_value() && flushed_again.has_value();
        co_await settle_collector(*io, collector, recovery_loop, exporter);
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(recovered);
    ASSERT_TRUE(drained);
    for (const auto count : requests)
        ASSERT_EQ(count, 2U);
    const auto statistics = exporter.statistics();
    ASSERT_TRUE(statistics.retries >= 1U);
    ASSERT_EQ(statistics.accepted_logs, 2U);
    ASSERT_EQ(statistics.accepted_metrics, 2U);
    ASSERT_EQ(statistics.accepted, 6U);
    ASSERT_EQ(statistics.exported, 6U);
    ASSERT_EQ(statistics.failed_batches, 0U);
    ASSERT_EQ(statistics.dropped, 0U);
}

TEST(exporter_statistics_are_visible_locally_without_recursive_export)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.endpoint = "http://127.0.0.1:1/v1/traces"}};
    ASSERT_TRUE(telemetry.submit_log({.body = "test"}));
    ASSERT_FALSE(telemetry.submit_metric({.name = "invalid", .value = -1, .kind = cnetmod::observability::otel_metric_kind::counter}));
    const auto before = telemetry.statistics();
    telemetry.refresh_exporter_metrics();
    const auto first = telemetry.metrics().render_openmetrics();
    telemetry.refresh_exporter_metrics();
    ASSERT_EQ(telemetry.metrics().render_openmetrics(), first);
    ASSERT_TRUE(first.contains("otel_exporter_accepted_logs_total 1\n"));
    ASSERT_TRUE(first.contains("otel_exporter_dropped_metrics_total 1\n"));
    ASSERT_TRUE(first.contains("otel_exporter_rejected_metric_points_total 0\n"));
    ASSERT_TRUE(first.contains("otel_exporter_invalid_responses_total 0\n"));
    ASSERT_EQ(telemetry.statistics().accepted, before.accepted);
    ASSERT_EQ(telemetry.statistics().dropped, before.dropped);
}

TEST(otlp_acknowledgements_validate_counts_structure_and_bounds)
{
    using cnetmod::observability::detail::parse_export_response;
    ASSERT_TRUE(parse_export_response("{}", "rejectedSpans", 2).has_value());
    for (const auto field : {"rejectedSpans", "rejectedDataPoints", "rejectedLogRecords"})
    {
        for (const auto value : {"1", "\"1\""})
        {
            const auto body = std::string{"{\"partialSuccess\":{\""} + field + "\":" + value + "}}";
            const auto result = parse_export_response(body, field, 2);
            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->rejected, 1U);
            ASSERT_TRUE(result->partial);
        }
    }
    for (const auto body : {"", "[]", "null", "{", "{}{}",
             "{\"partialSuccess\":true}",
             "{\"partialSuccess\":{\"rejectedSpans\":-1}}",
             "{\"partialSuccess\":{\"rejectedSpans\":1.5}}",
             "{\"partialSuccess\":{\"rejectedSpans\":\"18446744073709551616\"}}",
             "{\"partialSuccess\":{\"rejectedSpans\":3}}",
             "{\"partialSuccess\":{\"rejectedSpans\":1,\"rejectedSpans\":0}}",
             "{\"partialSuccess\":{},\"partialSuccess\":{}}",
             "{\"partialSuccess\":{\"errorMessage\":{}}}"})
        ASSERT_FALSE(parse_export_response(body, "rejectedSpans", 2).has_value());
    ASSERT_FALSE(parse_export_response(std::string(65537, ' '), "rejectedSpans", 2).has_value());
    ASSERT_FALSE(parse_export_response("{\"unknown\":" + std::string(17, '[') + "0" +
            std::string(17, ']') + "}",
        "rejectedSpans", 2)
            .has_value());
    const auto warning = parse_export_response(
        R"({"unknown":[{"future":true}],"partialSuccess":{"errorMessage":"private diagnostic"}})",
        "rejectedSpans", 2);
    ASSERT_TRUE(warning.has_value());
    ASSERT_TRUE(warning->warning);
    ASSERT_EQ(warning->rejected, 0U);
}

TEST(collector_partial_acceptance_counts_wire_records_without_retry)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    unsigned requests{};
    unsigned log_requests{};
    bool drained = true;
    cnetmod::observability::otlp_exporter_statistics statistics;
    cnetmod::http::router routes;
    routes.any("/*path", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            (void)co_await request.read_full_body();
            ++requests;
            std::string body;
            if (request.path() == "/v1/traces")
                body = R"({"partialSuccess":{"rejectedSpans":"1"}})";
            else if (request.path() == "/v1/metrics")
                body = R"({"partialSuccess":{"rejectedDataPoints":1}})";
            else
            {
                ++log_requests;
                body = log_requests == 1 ? R"({"partialSuccess":{"rejectedLogRecords":"1"}})"
                    : log_requests == 2  ? R"({"partialSuccess":{"errorMessage":"private diagnostic"}})"
                                         : R"({"partialSuccess":{"rejectedLogRecords":"99"}})";
            }
            request.json(cnetmod::http::status::ok, body);
            co_return;
        });
    cnetmod::http::server collector{*io};
    collector.set_router(std::move(routes));
    ASSERT_TRUE(collector.listen("127.0.0.1", 19434).has_value());
    auto accept_loop = collector.run();
    accept_loop.handle().resume();
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::observability::otlp_http_exporter exporter{*io,
            {.endpoint = "http://127.0.0.1:19434/v1/traces", .request_timeout = std::chrono::seconds{1}}};
        for (unsigned index = 0; index < 2; ++index)
        {
            auto active = cnetmod::http::tracing::start_client_span({}, "partial");
            drained = exporter.submit(cnetmod::http::tracing::finish_client_span(std::move(active))) && drained;
            drained = exporter.submit(cnetmod::observability::otel_log_record{.body = "test"}) && drained;
        }
        for (unsigned index = 0; index < 3; ++index)
            drained = exporter.submit(cnetmod::observability::otel_metric_record{
                          .name = index == 2 ? "second" : "first",
                          .value = 1}) &&
                drained;
        drained = (co_await exporter.flush(std::chrono::seconds{2})).has_value() && drained;
        for (unsigned index = 0; index < 2; ++index)
        {
            drained = exporter.submit(cnetmod::observability::otel_log_record{.body = "next"}) && drained;
            drained = (co_await exporter.flush(std::chrono::seconds{2})).has_value() && drained;
        }
        statistics = exporter.statistics();
        co_await settle_collector(*io, collector, accept_loop, exporter);
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(drained);
    ASSERT_EQ(requests, 5U);
    ASSERT_EQ(statistics.retries, 0U);
    ASSERT_EQ(statistics.exported, 4U);
    ASSERT_EQ(statistics.rejected_spans, 1U);
    ASSERT_EQ(statistics.rejected_metric_points, 1U);
    ASSERT_EQ(statistics.rejected_logs, 1U);
    ASSERT_EQ(statistics.partial_batches, 4U);
    ASSERT_EQ(statistics.warning_batches, 1U);
    ASSERT_EQ(statistics.invalid_responses, 1U);
    ASSERT_EQ(statistics.failed_batches, 1U);
}

TEST(otlp_response_budget_rejects_excess_and_accepts_exact_boundary)
{
    for (unsigned mode = 0; mode < 20; ++mode)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        const auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        bool closed_early{};
        bool drained{};
        bool flush_timed_out{};
        unsigned finished{};
        cnetmod::observability::otlp_exporter_statistics statistics;
        auto finish = [&]
        {
            if (++finished == 2)
                io->stop();
        };
        auto collector = [&]() -> cnetmod::task<void>
        {
            auto peer = co_await cnetmod::async_accept(*io, *listener);
            if (!peer)
            {
                io->stop();
                co_return;
            }
            std::string input;
            while (input.find("\r\n\r\n") == std::string::npos)
            {
                char bytes[4096];
                const auto read = co_await cnetmod::async_read(*io, *peer, cnetmod::mutable_buffer{bytes, sizeof(bytes)});
                if (!read || *read == 0)
                {
                    io->stop();
                    co_return;
                }
                input.append(bytes, *read);
            }
            // Consume the request before closing a deliberately truncated
            // response. Closing with unread request bytes produces TCP RST on
            // Linux, which tests a transport failure instead of HTTP framing.
            const auto header_end = input.find("\r\n\r\n") + 4;
            auto headers = input.substr(0, header_end);
            for (auto& character : headers)
                if (character >= 'A' && character <= 'Z')
                    character += 'a' - 'A';
            const auto length_header = headers.find("content-length:");
            ASSERT_NE(length_header, std::string::npos);
            if (length_header == std::string::npos)
            {
                io->stop();
                co_return;
            }
            auto length_start = length_header + std::string_view{"content-length:"}.size();
            while (headers[length_start] == ' ')
                ++length_start;
            std::size_t request_length{};
            const auto parsed = std::from_chars(headers.data() + length_start,
                headers.data() + headers.find("\r\n", length_start), request_length);
            ASSERT_TRUE(parsed.ec == std::errc{});
            while (input.size() - header_end < request_length)
            {
                char bytes[4096];
                const auto read = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{bytes, sizeof(bytes)});
                if (!read || *read == 0)
                {
                    io->stop();
                    co_return;
                }
                input.append(bytes, *read);
            }
            std::string reply = mode != 0
                ? "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10001\r\n"
                : "HTTP/1.1 200 OK\r\nContent-Length: 65537\r\n\r\n";
            if (mode == 2)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n8000\r\n" +
                    std::string(32768, ' ') + "\r\n8000\r\n" + std::string(32768, ' ') + "\r\n1\r\n";
            if (mode == 3)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10000\r\n" +
                    std::string(65534, ' ') + "{}\r\n0\r\n\r\n";
            if (mode == 4)
                reply = "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\n\r\n{}";
            if (mode == 5)
                reply = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n{}";
            if (mode == 6)
                reply = "HTTP/1.1 200 OK\r\nContent-Length: 2junk\r\n\r\n{}";
            if (mode == 7)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2x\r\n{}\r\n0\r\n\r\n";
            if (mode == 8)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}xx0\r\n\r\n";
            if (mode == 9)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n";
            if (mode == 10)
                reply = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n{}";
            if (mode == 11)
                reply = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" + std::string(65537, ' ');
            if (mode == 12)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2";
            if (mode == 13)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{";
            if (mode == 14)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r";
            if (mode == 15)
                reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n";
            if (mode == 17)
            {
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{100});
                reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
            }
            // The silent collector keeps the connection open until cancellation.
            if (mode == 19)
                reply = "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 30\r\nContent-Length: 2\r\n\r\n{}";
            if (mode != 16 && mode != 18)
                (void)co_await cnetmod::async_write_all(*io, *peer, cnetmod::const_buffer{reply.data(), reply.size()});
            if (mode >= 5 && mode < 16)
            {
                peer->close();
                closed_early = true;
                finish();
                co_return;
            }
            // Oversized cases omit the excess body; rejection must close rather
            // than wait for it. Complete responses close with exporter teardown.
            for (;;)
            {
                char bytes[4096];
                const auto read = co_await cnetmod::async_read(*io, *peer, cnetmod::mutable_buffer{bytes, sizeof(bytes)});
                if (!read || *read == 0)
                {
                    closed_early = true;
                    break;
                }
            }
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            cnetmod::observability::otlp_http_exporter exporter{*io,
                {.endpoint = mode >= 18 ? std::format("http://localhost:{}/v1/traces", endpoint->port()) : "",
                    .logs_endpoint = std::format("http://{}:{}/v1/logs", mode >= 17 ? "localhost" : "127.0.0.1", endpoint->port()),
                    .request_timeout = std::chrono::milliseconds{mode == 16 ? 100 : 2000},
                    .max_attempts = mode == 16 ? 1U : 3U}};
            drained = exporter.submit(cnetmod::observability::otel_log_record{.body = "test"});
            if (mode >= 18)
            {
                if (mode == 19)
                {
                    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                    while (exporter.statistics().retries == 0 && std::chrono::steady_clock::now() < limit)
                        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                    ASSERT_EQ(exporter.statistics().retries, 1U);
                }
                const auto first = co_await exporter.flush(std::chrono::milliseconds{20});
                flush_timed_out = !first && first.error() == std::make_error_code(std::errc::timed_out);
                drained = exporter.submit(cnetmod::observability::otel_log_record{.body = "queued"}) && drained;
                auto span = cnetmod::http::tracing::start_client_span({}, "queued");
                drained = exporter.submit(cnetmod::http::tracing::finish_client_span(std::move(span))) && drained;
                drained = exporter.submit(cnetmod::observability::otel_metric_record{.name = "queued", .value = 1}) && drained;
                if (mode == 19)
                {
                    ASSERT_FALSE(exporter.try_settle_shutdown());
                    ASSERT_FALSE(exporter.try_settle_shutdown());
                }
            }
            if (mode == 17)
            {
                const auto first = co_await exporter.flush(std::chrono::milliseconds{20});
                flush_timed_out = !first && first.error() == std::make_error_code(std::errc::timed_out);
                // Closing acceptance must preserve the already accepted record.
                exporter.close();
            }
            if (mode == 17 || mode == 18)
                drained = (co_await exporter.shutdown(std::chrono::milliseconds{mode == 18 ? 0 : 3000},
                               std::chrono::milliseconds{500}))
                              .has_value() &&
                    drained;
            else
                drained = (co_await exporter.flush(std::chrono::milliseconds{mode >= 18 ? 500 : 3000})).has_value() && drained;
            if (mode >= 18)
            {
                ASSERT_TRUE(exporter.try_settle_shutdown());
                ASSERT_FALSE(exporter.submit(cnetmod::observability::otel_log_record{.body = "closed"}));
            }
            if (mode >= 17)
            {
                const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
                while (!closed_early && std::chrono::steady_clock::now() < limit)
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                ASSERT_TRUE(closed_early);
            }
            statistics = exporter.statistics();
            exporter.close();
            // A localhost connection may resolve only to an address family
            // that the IPv4 fixture does not serve. Closing the listener makes
            // a still-pending accept observable instead of leaving io.run()
            // alive until the outer CTest timeout.
            listener->close();
            finish();
        };
        cnetmod::spawn(*io, collector());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(drained);
        ASSERT_TRUE(closed_early);
        if (mode >= 17)
            ASSERT_TRUE(flush_timed_out);
        const bool valid = mode == 3 || mode == 10 || mode == 17;
        ASSERT_EQ(statistics.invalid_responses, valid || mode == 16 || mode >= 18 ? 0U : 1U);
        ASSERT_EQ(statistics.failed_batches, valid ? 0U : 1U);
        ASSERT_EQ(statistics.retries, mode == 19 ? 1U : 0U);
        ASSERT_EQ(statistics.exported, valid ? 1U : 0U);
        if (mode >= 18)
        {
            ASSERT_EQ(statistics.accepted, 4U);
            ASSERT_EQ(statistics.dropped_logs, 2U);
            ASSERT_EQ(statistics.dropped_spans, 1U);
            ASSERT_EQ(statistics.dropped_metrics, 1U);
            ASSERT_EQ(statistics.dropped, 4U);
        }
    }
}

TEST(cancellable_dns_capacity_rejects_before_launching_system_lookup)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();

    struct restore_configuration
    {
        ~restore_configuration()
        {
            cnetmod::configure_dns_cache({});
        }
    } restore;

    cnetmod::configure_dns_cache({.enabled = false, .max_pending_lookups = 0});
    const auto before = cnetmod::get_dns_cache_metrics();
    bool rejected{};
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token token;
        const auto result = co_await cnetmod::async_connect_happy_eyeballs(
            *io, "localhost", 1, {}, token);
        rejected = !result && result.error() == std::make_error_code(std::errc::host_unreachable);
        io->stop();
    };
    cnetmod::spawn(*io, exercise());
    io->run();
    const auto after = cnetmod::get_dns_cache_metrics();
    ASSERT_TRUE(rejected);
    ASSERT_EQ(after.pending_lookups, before.pending_lookups);
    ASSERT_EQ(after.rejected_lookups, before.rejected_lookups + 1);
}

RUN_TESTS()
