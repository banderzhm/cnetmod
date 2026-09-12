/// Trace-aware decorator for outbound HTTP requests.
export module cnetmod.observability.http;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::observability {

/// Decorator pattern: preserves http::client behavior and ownership while
/// adding W3C propagation and one CLIENT span per request.
export class instrumented_http_client
{
public:
    instrumented_http_client(http::client& client,
        http::tracing::span_exporter exporter) noexcept;

    [[nodiscard]] auto send(const http::request& request,
        const http::tracing::trace_context& parent)
        -> task<std::expected<http::response, std::error_code>>;
    [[nodiscard]] auto send(const http::request& request,
        const http::tracing::trace_context& parent, cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

private:
    void report(http::tracing::active_span span, bool failed) const noexcept;

    http::client* client_;
    http::tracing::span_exporter exporter_;
};

} // namespace cnetmod::observability
