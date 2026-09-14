/**
 * @brief Optional tracing and metrics for outbound HTTP requests.
 */
export module cnetmod.observability.http;

import std;
import cnetmod.instrumentation.metric;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::observability {

/**
 * @brief Decorates HTTP requests with independently enabled traces and metrics.
 *
 * Empty sinks select the original client task directly. Metrics alone do not
 * create trace identifiers, copy requests, or inject propagation headers.
 */
export class instrumented_http_client
{
public:
    instrumented_http_client(http::client& client,
        http::tracing::span_exporter exporter,
        instrumentation::metric_sink metrics = {}) noexcept;

    [[nodiscard]] auto send(const http::request& request,
        const http::tracing::trace_context& parent)
        -> task<std::expected<http::response, std::error_code>>;
    [[nodiscard]] auto send(const http::request& request,
        const http::tracing::trace_context& parent, cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

private:
    /**
     * @brief Runs the instrumented branch after the allocation-free dispatch.
     * The frame owns its parent context; request and cancellation remain borrowed.
     */
    auto send_observed(const http::request& request,
        http::tracing::trace_context parent, cancel_token* cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    http::client* client_;
    http::tracing::span_exporter exporter_;
    instrumentation::metric_sink metrics_;
};

} // namespace cnetmod::observability
