/// W3C Trace Context propagation for HTTP handlers and outbound requests.
///
/// This module deliberately exposes a small exporter callback instead of
/// linking an OpenTelemetry SDK. Applications can forward completed spans to
/// OpenTelemetry, their logger, or a collector without making cnetmod's core
/// dependency graph heavier.
export module cnetmod.protocol.http.middleware.tracing;

import std;
import cnetmod.protocol.http;

namespace cnetmod::http::tracing {

export struct trace_context
{
    std::string trace_id;
    std::string span_id;
    std::uint8_t flags{};
    std::string tracestate;
};

/// OpenTelemetry span role. Keeping this protocol-neutral enum in cnetmod's
/// tracing API prevents a public dependency on a particular telemetry SDK.
export enum class span_kind
{
    unspecified,
    internal,
    server,
    client,
    producer,
    consumer
};

/// Parse a W3C traceparent header. Invalid input is intentionally ignored by
/// the server middleware, which then begins a new root trace.
export [[nodiscard]] auto parse_traceparent(std::string_view value,
    std::string_view tracestate = {}) -> std::optional<trace_context>;

/// Create a new root trace or a child span that preserves trace identity and
/// sampling flags.
export [[nodiscard]] auto new_root_context() -> trace_context;
export [[nodiscard]] auto child_context(const trace_context& parent)
    -> trace_context;
export [[nodiscard]] auto format_traceparent(const trace_context& context)
    -> std::string;

/// Write the standard propagation headers. The request is caller-owned, so
/// injection never changes client behavior other than the two trace headers.
export void inject(request& destination, const trace_context& context);

/// Recover the active server span from a request context. An empty optional
/// means the tracing middleware was not installed for this route.
export [[nodiscard]] auto context_from(const request_context& request)
    -> std::optional<trace_context>;

export struct completed_span
{
    trace_context context;
    /// Empty for the HTTP middleware's compatibility shape. Explicit client
    /// spans use this as their stable OpenTelemetry operation name.
    std::string name;
    std::string method;
    std::string path;
    int status_code{};
    std::chrono::steady_clock::duration elapsed{};
    bool has_remote_parent{};
    bool failed{};
    std::vector<std::pair<std::string, std::string>> attributes;
    /// Direct parent identity is separate from the current span context and
    /// is required to reconstruct a distributed trace tree in a collector.
    std::string parent_span_id;
    std::chrono::system_clock::time_point started_at{};
    std::chrono::system_clock::time_point ended_at{};
    span_kind kind{span_kind::unspecified};
};

export using span_exporter = std::function<void(const completed_span&)>;

/// Explicit span handle for coroutine code. It carries all state in the
/// caller-owned object, rather than a thread-local scope, so a coroutine can
/// resume on a different worker without losing or corrupting its trace.
export struct active_span
{
    trace_context context;
    std::string name;
    std::chrono::steady_clock::time_point started;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string parent_span_id;
    std::chrono::system_clock::time_point started_at{};
    span_kind kind{span_kind::client};
};

/// Start and finish a child client span. Database and Redis protocols have no
/// on-the-wire traceparent field, but these helpers retain the same trace ID
/// and produce collector-ready spans through the application's exporter.
export [[nodiscard]] auto start_client_span(const trace_context& parent,
    std::string name,
    std::vector<std::pair<std::string, std::string>> attributes = {}) -> active_span;
export [[nodiscard]] auto finish_client_span(active_span span,
    bool failed = false) -> completed_span;

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

/// Establish one server span per request. This is opt-in middleware, so it
/// adds no allocation or parsing work to servers that do not use tracing.
export [[nodiscard]] auto tracing_middleware(tracing_options options = {})
    -> middleware_fn;

} // namespace cnetmod::http::tracing
