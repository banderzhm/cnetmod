/**
 * @brief Protocol-independent trace identity and completed span records.
 */
export module cnetmod.instrumentation.tracing;

import std;
export import cnetmod.instrumentation.operation_result;

namespace cnetmod::instrumentation {

export struct trace_context
{
    std::string trace_id;
    std::string span_id;
    std::uint8_t flags{};
    std::string tracestate;
};

/**
 * OpenTelemetry span role. Keeping this protocol-neutral enum in cnetmod's
 * tracing API prevents a public dependency on a particular telemetry SDK.
 */
export enum class span_kind
{
    unspecified,
    internal,
    server,
    client,
    producer,
    consumer
};

/**
 * Parse a W3C traceparent header. Invalid input is intentionally ignored by
 * the server middleware, which then begins a new root trace.
 */
export [[nodiscard]] auto parse_traceparent(std::string_view value,
    std::string_view tracestate = {}) -> std::optional<trace_context>;

/**
 * Create a new root trace or a child span that preserves trace identity and
 * sampling flags.
 */
export [[nodiscard]] auto new_root_context() -> trace_context;
export [[nodiscard]] auto child_context(const trace_context& parent)
    -> trace_context;
export [[nodiscard]] auto format_traceparent(const trace_context& context)
    -> std::string;

export struct completed_span
{
    trace_context context;
    /**
     * Empty for the HTTP middleware's compatibility shape. Explicit client
     * spans use this as their stable OpenTelemetry operation name.
     */
    std::string name;
    std::string method;
    std::string path;
    int status_code{};
    std::chrono::steady_clock::duration elapsed{};
    bool has_remote_parent{};
    bool failed{};
    std::vector<std::pair<std::string, std::string>> attributes;
    /**
     * Direct parent identity is separate from the current span context and
     * is required to reconstruct a distributed trace tree in a collector.
     */
    std::string parent_span_id;
    std::chrono::system_clock::time_point started_at{};
    std::chrono::system_clock::time_point ended_at{};
    span_kind kind{span_kind::unspecified};
    operation_result result;
};

/**
 * @brief Couples a completion sink with a root sampling decision made at span start.
 *
 * Children inherit their parent's sampled flag. An empty sink remains the
 * disabled fast path; sampling never uses thread-local state.
 */
export class span_exporter
{
public:
    using sink_function = std::function<void(const completed_span&)>;
    using root_sampler = std::function<bool(const trace_context&)>;

    span_exporter() noexcept = default;
    span_exporter(sink_function sink, root_sampler sampler);

    template <typename Sink>
    requires(!std::same_as<std::remove_cvref_t<Sink>, span_exporter> &&
        std::is_invocable_r_v<void, Sink, const completed_span&>)
    span_exporter(Sink&& sink) : sink_(std::forward<Sink>(sink))
    {}

    explicit operator bool() const noexcept;
    void operator()(const completed_span& span) const;
    /**
     * @brief Sets only the sampled bit, containing root sampler exceptions.
     */
    auto sample(trace_context& context, bool has_parent) const noexcept -> bool;

private:
    sink_function sink_;
    root_sampler sampler_;
};

/**
 * Explicit span handle for coroutine code. It carries all state in the
 * caller-owned object, rather than a thread-local scope, so a coroutine can
 * resume on a different worker without losing or corrupting its trace.
 */
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

/**
 * Start and finish a child client span. Database and Redis protocols have no
 * on-the-wire traceparent field, but these helpers retain the same trace ID
 * and produce collector-ready spans through the application's exporter.
 */
export [[nodiscard]] auto start_client_span(const trace_context& parent,
    std::string name,
    std::vector<std::pair<std::string, std::string>> attributes = {}) -> active_span;
/**
 * @brief Starts a child server span for protocol adapters above HTTP routing.
 *
 * The caller owns propagation.  This helper only expresses the remote-server
 * role without coupling core tracing to a concrete transport.
 */
export [[nodiscard]] auto start_server_span(const trace_context& parent,
    std::string name,
    std::vector<std::pair<std::string, std::string>> attributes = {}) -> active_span;
export [[nodiscard]] auto finish_client_span(active_span span,
    bool failed = false) -> completed_span;

/**
 * @brief Validates identity without allocating or changing the context.
 */
export [[nodiscard]] auto valid_trace_context(const trace_context& context) noexcept -> bool;

} // namespace cnetmod::instrumentation
