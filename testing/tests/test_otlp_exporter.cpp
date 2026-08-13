#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.core.socket;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.observability.otlp;
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

namespace {

struct collector_observation
{
    std::size_t requests{};
    std::string content_type;
    std::string payload;
    bool flush_succeeded{};
};

auto export_to_local_collector(cnetmod::io_context& context,
    cnetmod::http::server& collector, collector_observation& observation,
    std::uint16_t port) -> cnetmod::task<void>
{
    cnetmod::observability::otlp_http_exporter exporter{context,
        {.endpoint = "http://127.0.0.1:" + std::to_string(port) + "/v1/traces",
            .service_name = "otlp-wire-e2e",
            .queue_capacity = 8U,
            .max_batch_size = 8U,
            .request_timeout = std::chrono::seconds{2}}};
    const auto trace = cnetmod::http::tracing::new_root_context();
    const cnetmod::http::tracing::completed_span first{
        .context = trace,
        .method = "POST",
        .path = "/orders/42",
        .status_code = 201,
        .elapsed = std::chrono::milliseconds{3},
    };
    auto child = cnetmod::http::tracing::child_context(trace);
    const cnetmod::http::tracing::completed_span second{
        .context = std::move(child),
        .method = "GET",
        .path = "/orders/42",
        .status_code = 200,
        .elapsed = std::chrono::milliseconds{1},
        .has_remote_parent = true,
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
            observation.payload = std::string{co_await request.read_full_body()};
            ++observation.requests;
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
    ASSERT_EQ(observation.requests, std::size_t{1});
    ASSERT_EQ(observation.content_type, "application/json");
    ASSERT_TRUE(observation.payload.contains("\"resourceSpans\""));
    ASSERT_TRUE(observation.payload.contains("\"service.name\""));
    ASSERT_TRUE(observation.payload.contains("otlp-wire-e2e"));
    ASSERT_TRUE(observation.payload.contains("POST /orders/42"));
    ASSERT_TRUE(observation.payload.contains("GET /orders/42"));
    ASSERT_TRUE(observation.payload.contains("REDIS GET"));
    ASSERT_TRUE(observation.payload.contains("db.system"));
    ASSERT_TRUE(observation.payload.contains("STATUS_CODE_ERROR"));
    ASSERT_TRUE(observation.payload.contains("\"traceId\""));
}

RUN_TESTS()
