#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.core.socket;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.observability.otlp;
import cnetmod.observability;
import cnetmod.protocol.http.middleware.tracing;

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

namespace {

struct collector_observation
{
    std::size_t requests{};
    std::string content_type;
    std::string payload;
    bool flush_succeeded{};
    std::string authorization;
    std::uint64_t retries{};
    std::string expected_parent_span_id;
};

auto export_to_local_collector(cnetmod::io_context& context,
    cnetmod::http::server& collector, collector_observation& observation,
    std::uint16_t port) -> cnetmod::task<void>
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
            .initial_retry_delay = std::chrono::milliseconds{1}}};
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
    if (exporter.submit(first) && exporter.submit(second) && exporter.submit(redis))
    {
        const auto flushed = co_await exporter.flush(std::chrono::seconds{2});
        observation.flush_succeeded = flushed.has_value();
        observation.retries = exporter.statistics().retries;
    }
    exporter.close();
    collector.stop();
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
    routes.post("/v1/traces", [&observation](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            observation.content_type = request.get_header("content-type");
            observation.authorization = request.get_header("authorization");
            observation.payload = std::string{co_await request.read_full_body()};
            ++observation.requests;
            if (observation.requests == 1U)
            {
                request.resp().set_header("Retry-After", "0");
                request.text(cnetmod::http::status::service_unavailable,
                    "retry");
            }
            else
                request.text(cnetmod::http::status::accepted, "accepted");
            co_return;
        });
    cnetmod::http::server collector{*context};
    collector.set_router(std::move(routes));
    ASSERT_TRUE(collector.listen("127.0.0.1", collector_port).has_value());

    cnetmod::spawn(*context, collector.run());
    cnetmod::spawn(*context, export_to_local_collector(*context, collector, observation, collector_port));
    context->run();

    ASSERT_TRUE(observation.flush_succeeded);
    ASSERT_EQ(observation.requests, std::size_t{2});
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
}

RUN_TESTS()
