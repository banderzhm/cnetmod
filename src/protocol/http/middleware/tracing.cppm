/**
 * @brief HTTP middleware and carrier adapters for protocol-independent tracing.
 */
export module cnetmod.protocol.http.middleware.tracing;

import std;
import cnetmod.protocol.http;
import cnetmod.instrumentation.tracing;

namespace cnetmod::http::tracing {

export using cnetmod::instrumentation::trace_context;
export using cnetmod::instrumentation::span_kind;
export using cnetmod::instrumentation::completed_span;
export using cnetmod::instrumentation::active_span;
export using cnetmod::instrumentation::span_exporter;
export using cnetmod::instrumentation::parse_traceparent;
export using cnetmod::instrumentation::new_root_context;
export using cnetmod::instrumentation::child_context;
export using cnetmod::instrumentation::format_traceparent;
export using cnetmod::instrumentation::start_client_span;
export using cnetmod::instrumentation::finish_client_span;

/// Write the standard propagation headers. The request is caller-owned, so
/// injection never changes client behavior other than the two trace headers.
export void inject(request& destination, const trace_context& context);

/// Recover the active server span from a request context. An empty optional
/// means the tracing middleware was not installed for this route.
export [[nodiscard]] auto context_from(const request_context& request)
    -> std::optional<trace_context>;

export struct tracing_options
{
    /// Return the server span to the caller. This is useful for propagating
    /// through reverse proxies and is disabled only when an application has a
    /// stricter response-header policy.
    bool emit_response_traceparent{true};
    /// Trace Context permits tracestate to be optional. Setting this false
    /// avoids retaining vendor-specific state at a trust boundary.
    bool accept_tracestate{true};
    /// Called after the route has completed (also during exception unwinding).
    /// Throwing from an exporter is ignored to preserve the HTTP request.
    span_exporter on_end;
};

/**
 * @brief Establishes one server span per request when an exporter is supplied.
 *
 * An empty on_end callback returns an empty middleware. server::use ignores
 * empty middleware, leaving the request pipeline unchanged. Custom pipelines
 * must check the returned callable before invoking it and keep it alive until
 * all tasks it creates have completed. Recoverable trace preparation failures
 * fall back to the original handler; handler exceptions retain their identity.
 */
export [[nodiscard]] auto tracing_middleware(tracing_options options = {})
    -> middleware_fn;

} // namespace cnetmod::http::tracing
