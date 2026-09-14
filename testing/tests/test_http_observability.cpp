#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.core.error;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.instrumentation.metric;
import cnetmod.instrumentation.operation_result;
import cnetmod.observability;
import cnetmod.observability.otlp;
import cnetmod.observability.http;
import cnetmod.observability.http_server;
import cnetmod.application.http_client;
import cnetmod.application.managed_service;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;

namespace {

struct observation
{
    unsigned requests{};
    unsigned traced{};
    bool responses_match{true};
    std::vector<cnetmod::instrumentation::metric_measurement> measurements;
    std::vector<cnetmod::instrumentation::metric_measurement> server_measurements;
    unsigned spans{};
    bool application_metrics{};
    bool admission_response_preserved{};
    bool handler_exception_contained{};
    bool cancellations_match{true};
    unsigned cancelled_spans{};
    unsigned cancelled_measurements{};
    std::vector<std::error_code> cancellation_errors;
};

auto exercise_clients(cnetmod::io_context& io, cnetmod::http::server& server,
    observation& result) -> cnetmod::task<void>
{
    cnetmod::http::client raw{io};
    const auto parent = cnetmod::http::tracing::new_root_context();
    for (unsigned mode{}; mode < 4U; ++mode)
    {
        cnetmod::instrumentation::metric_sink metrics;
        cnetmod::http::tracing::span_exporter spans;
        if ((mode & 1U) != 0U)
            metrics = [&](auto measurement)
            {
                result.measurements.push_back(std::move(measurement));
                throw std::runtime_error("metric sink failure");
            };
        if ((mode & 2U) != 0U)
            spans = [&](const auto&)
            {
                ++result.spans;
                throw std::runtime_error("span sink failure");
            };
        cnetmod::observability::instrumented_http_client client{raw, spans, metrics};
        for (auto path : {"/ok", "/fail"})
        {
            cnetmod::http::request request{cnetmod::http::http_method::GET,
                std::string{"http://127.0.0.1:19432"} + path + "?secret=private"};
            request.set_header("Authorization", "Bearer private");
            cnetmod::cancel_token cancellation;
            auto response = mode % 2U == 0U ? co_await client.send(request, parent)
                                            : co_await client.send(request, parent, cancellation);
            result.responses_match = result.responses_match && response.has_value() &&
                response->body() == "unchanged" &&
                response->status_code() == (std::string_view{path} == "/ok" ? 200 : 503) &&
                response->get_header("traceparent").empty() &&
                request.get_header("traceparent").empty();
        }
    }
    for (unsigned mode = 0; mode != 4; ++mode)
    {
        cnetmod::cancel_token cancellation;
        cancellation.cancel();
        const cnetmod::http::request request{cnetmod::http::http_method::GET,
            "http://127.0.0.1:19432/ok"};
        const auto baseline = co_await raw.send(request, cancellation);
        cnetmod::http::tracing::span_exporter spans;
        cnetmod::instrumentation::metric_sink metrics;
        if ((mode & 2U) != 0U)
            spans = [&](const cnetmod::http::tracing::completed_span& span)
            {
                ++result.cancelled_spans;
                result.cancellations_match = result.cancellations_match &&
                    span.result.status == cnetmod::instrumentation::operation_status::cancelled &&
                    span.result.error == cnetmod::make_error_code(cnetmod::errc::operation_aborted) && !span.failed;
                throw std::runtime_error("cancelled span exporter failure");
            };
        if ((mode & 1U) != 0U)
            metrics = [&](auto metric)
            {
                ++result.cancelled_measurements;
                bool cancelled = false;
                for (const auto& [key, value] : metric.attributes)
                {
                    if (key == "error.type")
                        cancelled = value == "cancelled";
                    if (key == "http.response.status_code")
                        result.cancellations_match = false;
                }
                result.cancellations_match = result.cancellations_match && cancelled;
                throw std::runtime_error("cancelled metric exporter failure");
            };
        cnetmod::observability::instrumented_http_client client{raw, spans, metrics};
        const auto observed = co_await client.send(request, parent, cancellation);
        result.cancellation_errors.push_back(baseline ? std::error_code{} : baseline.error());
        result.cancellation_errors.push_back(observed ? std::error_code{} : observed.error());
        result.cancellations_match = result.cancellations_match && !baseline && !observed &&
            baseline.error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted) && observed.error() == baseline.error() &&
            request.get_header("traceparent").empty();
    }
    const auto failed_handler = co_await raw.get("http://127.0.0.1:19432/throw");
    result.handler_exception_contained = !failed_handler;
    cnetmod::observability::telemetry_hub telemetry{io};
    cnetmod::application::http_client_service service{io, telemetry, {}, "default",
        cnetmod::application::service_requirement::required};
    const cnetmod::http::request request{cnetmod::http::http_method::GET,
        "http://127.0.0.1:19432/ok"};
    const auto service_response = co_await service.client().send(request, parent);
    result.application_metrics = service_response.has_value() &&
        telemetry.metrics().render_openmetrics().contains("http_client_request_duration_seconds_count");
    server.set_max_connections(1);
    cnetmod::http::client excess{io};
    const auto rejected = co_await excess.send(request);
    ASSERT_TRUE(rejected.has_value());
    if (!rejected)
        ASSERT_EQ(rejected.error(), std::error_code{});
    if (rejected)
    {
        ASSERT_EQ(rejected->status_code(), cnetmod::http::status::too_many_requests);
        ASSERT_EQ(rejected->get_header("Connection"), "close");
        ASSERT_EQ(rejected->get_header("Retry-After"), "1");
    }
    result.admission_response_preserved = rejected &&
        rejected->status_code() == cnetmod::http::status::too_many_requests &&
        rejected->get_header("Connection") == "close" &&
        rejected->get_header("Retry-After") == "1";
    excess.close();
    service.raw_client().close();
    raw.close();
    server.stop();
}

} // namespace

TEST(http_metrics_and_traces_are_independent_and_preserve_responses)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    observation result;
    cnetmod::http::router routes;
    routes.any("/*path", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            if (request.path() == "/throw")
                throw std::runtime_error("private-handler-detail");
            ++result.requests;
            result.responses_match = result.responses_match && request.trace_id().empty();
            if (!request.get_header("traceparent").empty())
                ++result.traced;
            request.text(request.path() == "/ok" ? cnetmod::http::status::ok : cnetmod::http::status::service_unavailable, "unchanged");
            co_return;
        });
    cnetmod::http::server server{*io};
    server.use(cnetmod::http::tracing::tracing_middleware());
    server.use(cnetmod::observability::server_metrics({}));
    server.use(cnetmod::observability::server_metrics([&](auto measurement)
        {
            result.server_measurements.push_back(std::move(measurement));
            throw std::runtime_error("server sink failure");
        }));
    server.set_router(std::move(routes));
    ASSERT_TRUE(server.listen("127.0.0.1", 19432).has_value());
    auto listener = server.run();
    listener.handle().resume();
    auto run = [&]() -> cnetmod::task<void>
    {
        co_await exercise_clients(*io, server, result);
        while (!listener.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        listener.handle().promise().result();
        while (server.active_connections() != 0)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };
    auto clients = run();
    clients.handle().resume();
    io->run();
    ASSERT_TRUE(listener.handle().done());
    ASSERT_TRUE(clients.handle().done());
    clients.handle().promise().result();
    ASSERT_EQ(server.active_connections(), 0U);
    ASSERT_TRUE(result.responses_match);
    for (const auto& error : result.cancellation_errors)
        ASSERT_TRUE(error == cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    ASSERT_TRUE(result.cancellations_match);
    ASSERT_EQ(result.cancelled_spans, 2U);
    ASSERT_EQ(result.cancelled_measurements, 2U);
    ASSERT_EQ(result.requests, 9U);
    ASSERT_TRUE(result.application_metrics);
    ASSERT_TRUE(result.handler_exception_contained);
    ASSERT_TRUE(result.admission_response_preserved);
    ASSERT_EQ(result.traced, 4U);
    ASSERT_EQ(result.spans, 4U);
    ASSERT_EQ(result.measurements.size(), 4U);
    ASSERT_EQ(result.server_measurements.size(), 10U);
    for (const auto& measurement : result.server_measurements)
    {
        ASSERT_EQ(measurement.name, "http.server.request.duration");
        ASSERT_EQ(measurement.unit, "s");
        ASSERT_TRUE(measurement.value >= 0.0);
    }
    unsigned failures{};
    for (const auto& metric : result.measurements)
    {
        ASSERT_EQ(metric.name, "http.client.request.duration");
        ASSERT_EQ(metric.unit, "s");
        ASSERT_TRUE(metric.kind == cnetmod::instrumentation::metric_kind::histogram);
        ASSERT_TRUE(metric.value >= 0.0);
        ASSERT_EQ(metric.explicit_bounds.size(), 14U);
        for (const auto& [key, value] : metric.attributes)
        {
            ASSERT_FALSE(value.contains("private"));
            ASSERT_FALSE(key.contains("url"));
            if (key == "error.type")
            {
                ++failures;
                ASSERT_EQ(value, "503");
            }
        }
    }
    ASSERT_EQ(failures, 2U);
}

TEST(admission_drain_is_bounded_and_stop_cancellable)
{
    cnetmod::net_init network;
    for (const bool cancel : {false, true})
    {
        auto io = cnetmod::make_io_context();
        auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(reservation.has_value());
        ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        const auto endpoint = reservation->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        reservation->close();
        cnetmod::http::server server{*io};
        server.set_max_connections(1);
        ASSERT_TRUE(server.listen("127.0.0.1", endpoint->port()).has_value());
        auto listener = server.run();
        listener.handle().resume();
        auto exercise = [&]() -> cnetmod::task<void>
        {
            auto admitted = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
            ASSERT_TRUE(admitted.has_value());
            ASSERT_TRUE((co_await cnetmod::async_connect(*io, *admitted, *endpoint)).has_value());
            while (server.active_connections() == 0)
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
            auto rejected = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
            ASSERT_TRUE(rejected.has_value());
            ASSERT_TRUE((co_await cnetmod::async_connect(*io, *rejected, *endpoint)).has_value());
            std::array<std::byte, 4096> bytes;
            const auto response = co_await cnetmod::async_read(*io, *rejected,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(response.has_value() && *response > 0);
            const auto began = std::chrono::steady_clock::now();
            if (!cancel)
            {
                // Leave the rejected peer's send half open past the drain budget.
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1100});
                cnetmod::http::client next{*io};
                const auto reply = co_await next.get(std::format("http://127.0.0.1:{}/", endpoint->port()));
                ASSERT_TRUE(reply.has_value());
                if (reply)
                    ASSERT_EQ(reply->status_code(), cnetmod::http::status::too_many_requests);
                next.close();
            }
            server.stop();
            while (!listener.handle().done())
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
            listener.handle().promise().result();
            if (cancel)
                ASSERT_TRUE(std::chrono::steady_clock::now() - began < std::chrono::milliseconds{500});
            rejected->close();
            admitted->close();
            while (server.active_connections() != 0)
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
            io->stop();
        };
        auto operation = exercise();
        operation.handle().resume();
        io->run();
        ASSERT_TRUE(operation.handle().done());
        operation.handle().promise().result();
        ASSERT_EQ(server.active_connections(), 0U);
    }
}

TEST(measurement_adapter_is_independent_of_tracing_and_safe_after_close)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub disabled{*io, {.export_metrics = false}};
    ASSERT_FALSE(static_cast<bool>(disabled.measurements()));
    cnetmod::observability::telemetry_hub local{*io};
    ASSERT_FALSE(static_cast<bool>(local.spans()));
    auto sink = local.measurements();
    ASSERT_TRUE(static_cast<bool>(sink));
    sink({.name = "http.client.request.duration", .value = 0.1, .kind = cnetmod::instrumentation::metric_kind::histogram, .unit = "s", .attributes = {{"http.request.method", "GET"}}, .explicit_bounds = {0.1, 1}});
    const auto before = local.metrics().render_openmetrics();
    ASSERT_TRUE(before.contains("http_client_request_duration_seconds_count"));
    ASSERT_TRUE(before.contains("http_request_method=\"GET\""));
    local.close();
    sink({.name = "after.close", .value = 1});
    ASSERT_EQ(local.metrics().render_openmetrics(), before);
    ASSERT_FALSE(static_cast<bool>(local.measurements()));
    cnetmod::instrumentation::metric_sink expired;
    {
        cnetmod::observability::telemetry_hub temporary{*io};
        expired = temporary.measurements();
    }
    expired({.name = "expired", .value = 1});
    cnetmod::observability::telemetry_hub exported{*io,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics", .export_traces = false}};
    ASSERT_FALSE(static_cast<bool>(exported.spans()));
    exported.measurements()({.name = "exported", .value = 2});
    ASSERT_EQ(exported.statistics().accepted_metrics, 1U);
    ASSERT_EQ(exported.statistics().accepted_spans, 0U);
}

TEST(http_metrics_preserve_transport_errors_without_a_trace_sink)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    std::vector<cnetmod::instrumentation::metric_measurement> measurements;
    cnetmod::observability::instrumented_http_client observed{raw, {}, [&](auto metric)
        {
            measurements.push_back(std::move(metric));
        }};
    bool same_error{};
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::request invalid{cnetmod::http::http_method::GET, "invalid://request"};
        const auto baseline = co_await raw.send(invalid);
        const auto instrumented = co_await observed.send(invalid, {});
        same_error = !baseline && !instrumented && baseline.error() == instrumented.error();
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(same_error);
    ASSERT_EQ(measurements.size(), 1U);
    ASSERT_TRUE(std::ranges::any_of(measurements[0].attributes, [](const auto& entry)
        {
            return entry.first == "error.type";
        }));
}

TEST(http_trace_parent_is_owned_until_delayed_execution)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    for (bool cancellable : {false, true})
    {
        std::vector<cnetmod::http::tracing::completed_span> spans;
        cnetmod::observability::instrumented_http_client observed{raw,
            [&](const auto& span)
            {
                spans.push_back(span);
            }};
        cnetmod::http::request request{cnetmod::http::http_method::GET, "invalid://request"};
        cnetmod::cancel_token cancellation;
        auto parent = cnetmod::http::tracing::new_root_context();
        const auto original = parent;
        auto pending = cancellable ? observed.send(request, parent, cancellation)
                                   : observed.send(request, parent);
        parent = {};
        bool completed = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            auto response = co_await std::move(pending);
            ASSERT_FALSE(response.has_value());
            completed = true;
            io->stop();
        };
        io->restart();
        cnetmod::spawn(*io, run());
        io->run();
        ASSERT_TRUE(completed);
        ASSERT_EQ(spans.size(), 1U);
        ASSERT_EQ(spans.front().context.trace_id, original.trace_id);
        ASSERT_EQ(spans.front().parent_span_id, original.span_id);
    }
}

TEST(server_measurements_preserve_exceptions_and_do_not_require_trace_context)
{
    ASSERT_FALSE(static_cast<bool>(cnetmod::observability::server_metrics({})));
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::header_map headers;
    for (int status : {200, 404, 503, 0})
    {
        cnetmod::http::response response;
        cnetmod::http::request_context request{*io, socket, "CUSTOM-private", "/private/123",
            headers, {}, response, {}};
        std::vector<cnetmod::instrumentation::metric_measurement> records;
        auto middleware = cnetmod::observability::server_metrics([&](auto measurement)
            {
                records.push_back(std::move(measurement));
                throw std::runtime_error("sink failure");
            });
        bool original_exception{};
        try
        {
            cnetmod::sync_wait(middleware(request, [&]() -> cnetmod::task<void>
                {
                    if (status == 0)
                        throw std::runtime_error("original handler failure");
                    response.set_status(status);
                    co_return;
                }));
        }
        catch (const std::runtime_error& error)
        {
            original_exception = std::string_view{error.what()} == "original handler failure";
        }
        ASSERT_EQ(original_exception, status == 0);
        ASSERT_TRUE(request.trace_id().empty());
        ASSERT_EQ(records.size(), 1U);
        bool has_status{};
        bool has_error{};
        for (const auto& [key, value] : records[0].attributes)
        {
            ASSERT_FALSE(value.contains("private"));
            if (key == "http.request.method")
                ASSERT_EQ(value, "_OTHER");
            if (key == "http.response.status_code")
            {
                has_status = true;
                ASSERT_EQ(value, std::to_string(status));
            }
            if (key == "error.type")
            {
                has_error = true;
                ASSERT_EQ(value, status == 0 ? "exception" : "503");
            }
        }
        ASSERT_EQ(has_status, status != 0);
        ASSERT_EQ(has_error, status == 0 || status >= 500);
    }
}

TEST(server_measurements_classify_cancelled_and_timed_out_handlers)
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::header_map headers;
    const std::array cases{
        std::pair{std::make_error_code(std::errc::operation_canceled), "cancelled"},
        std::pair{std::make_error_code(std::errc::timed_out), "timeout"},
        std::pair{cnetmod::make_error_code(cnetmod::errc::operation_aborted), "cancelled"},
        std::pair{cnetmod::make_error_code(cnetmod::errc::connection_timed_out), "timeout"}};
    for (const auto& [code, expected] : cases)
    {
        cnetmod::http::response response;
        cnetmod::http::request_context request{*io, socket, "GET", "/error", headers, {}, response, {}};
        std::vector<cnetmod::instrumentation::metric_measurement> records;
        auto middleware = cnetmod::observability::server_metrics([&](auto metric)
            {
                records.push_back(std::move(metric));
            });
        bool preserved = false;
        auto handler = [&]() -> cnetmod::task<void>
        {
            throw std::system_error(code);
            co_return;
        };
        try
        {
            cnetmod::sync_wait(middleware(request, handler));
        }
        catch (const std::system_error& error)
        {
            preserved = error.code() == code;
        }
        ASSERT_TRUE(preserved);
        ASSERT_EQ(records.size(), std::size_t{1});
        std::string failure;
        for (const auto& [key, value] : records[0].attributes)
        {
            ASSERT_FALSE(key == "http.response.status_code");
            if (key == "error.type")
                failure = value;
        }
        ASSERT_EQ(failure, expected);
    }
}

RUN_TESTS()
