#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;

namespace tracing = cnetmod::http::tracing;

TEST(http_traceparent_parses_and_normalizes)
{
    auto parsed = tracing::parse_traceparent(
        "00-4BF92F3577B34DA6A3CE929D0E0E4736-00F067AA0BA902B7-01",
        "vendor=value");

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->trace_id, "4bf92f3577b34da6a3ce929d0e0e4736");
    ASSERT_EQ(parsed->span_id, "00f067aa0ba902b7");
    ASSERT_EQ(parsed->flags, 1U);
    ASSERT_EQ(parsed->tracestate, "vendor=value");
}

TEST(http_traceparent_rejects_invalid_and_zero_identifiers)
{
    ASSERT_FALSE(tracing::parse_traceparent(
        "00-00000000000000000000000000000000-00f067aa0ba902b7-01")
            .has_value());
    ASSERT_FALSE(tracing::parse_traceparent(
        "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01")
            .has_value());
    ASSERT_FALSE(tracing::parse_traceparent(
        "ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01")
            .has_value());
    ASSERT_FALSE(tracing::parse_traceparent(
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
        "vendor=value\r\nX-Injected: true")
            .has_value());
}

TEST(http_trace_child_preserves_trace_identity_and_injects_headers)
{
    auto parent = tracing::new_root_context();
    parent.tracestate = "vendor=value";
    auto child = tracing::child_context(parent);

    ASSERT_EQ(child.trace_id, parent.trace_id);
    ASSERT_NE(child.span_id, parent.span_id);
    ASSERT_EQ(child.tracestate, parent.tracestate);

    cnetmod::http::request request(cnetmod::http::http_method::GET,
        "https://example.test/resource");
    tracing::inject(request, child);
    ASSERT_EQ(request.get_header("traceparent"), tracing::format_traceparent(child));
    ASSERT_EQ(request.get_header("tracestate"), "vendor=value");
}

TEST(tracing_explicit_client_span_preserves_parent_identity)
{
    const auto parent = tracing::new_root_context();
    auto active = tracing::start_client_span(parent, "REDIS GET",
        {{"db.system", "redis"}, {"db.operation", "GET"}});
    const auto completed = tracing::finish_client_span(std::move(active), true);

    ASSERT_EQ(completed.context.trace_id, parent.trace_id);
    ASSERT_NE(completed.context.span_id, parent.span_id);
    ASSERT_EQ(completed.name, "REDIS GET");
    ASSERT_EQ(completed.parent_span_id, parent.span_id);
    ASSERT_TRUE(completed.kind == tracing::span_kind::client);
    ASSERT_TRUE(completed.started_at.time_since_epoch().count() > 0);
    ASSERT_TRUE(completed.ended_at >= completed.started_at);
    ASSERT_TRUE(completed.failed);
    ASSERT_EQ(completed.attributes.size(), std::size_t{2});
    ASSERT_TRUE(completed.elapsed >= std::chrono::steady_clock::duration::zero());
}

TEST(http_tracing_middleware_creates_server_span_and_reports_it)
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::response response;
    cnetmod::http::header_map headers{
        {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
        {"tracestate", "vendor=value"},
    };
    cnetmod::http::request_context context(*io, socket, "GET", "/orders/42",
        headers, {}, response, {});
    std::optional<tracing::completed_span> reported;
    auto middleware = tracing::tracing_middleware({
        .on_end = [&reported](const tracing::completed_span& span)
        {
            reported = span;
        },
    });

    cnetmod::sync_wait(middleware(context, [&context]() -> cnetmod::task<void>
        {
            context.resp().set_status(cnetmod::http::status::accepted);
            co_return;
        }));

    auto server_context = tracing::context_from(context);
    ASSERT_TRUE(server_context.has_value());
    ASSERT_EQ(server_context->trace_id, "4bf92f3577b34da6a3ce929d0e0e4736");
    ASSERT_NE(server_context->span_id, "00f067aa0ba902b7");
    ASSERT_EQ(response.get_header("traceparent"),
        tracing::format_traceparent(*server_context));
    ASSERT_TRUE(reported.has_value());
    ASSERT_TRUE(reported->has_remote_parent);
    ASSERT_EQ(reported->parent_span_id, "00f067aa0ba902b7");
    ASSERT_TRUE(reported->kind == tracing::span_kind::server);
    ASSERT_TRUE(reported->started_at.time_since_epoch().count() > 0);
    ASSERT_TRUE(reported->ended_at >= reported->started_at);
    ASSERT_EQ(reported->status_code, cnetmod::http::status::accepted);
}

RUN_TESTS();
