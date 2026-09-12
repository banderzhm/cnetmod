/// Unified, SDK-neutral observability composition root.
///
/// Applications create one hub and pass its lightweight adapters to HTTP,
/// OpenAI, Redis, SQL, or their own components. Instrumented libraries stay
/// independent of an OpenTelemetry SDK while the process gets one resource,
/// one bounded exporter, and one metric registry.
export module cnetmod.observability;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability.otlp;
import cnetmod.protocol.http.middleware.metrics;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::observability {

class telemetry_hub_state;

export class telemetry_hub
{
public:
    telemetry_hub(io_context& context, otlp_http_options options = {});
    ~telemetry_hub();

    telemetry_hub(const telemetry_hub&) = delete;
    auto operator=(const telemetry_hub&) -> telemetry_hub& = delete;
    telemetry_hub(telemetry_hub&&) noexcept = default;
    auto operator=(telemetry_hub&&) noexcept -> telemetry_hub& = default;

    /// Non-owning process metric registry used by protocol adapters and the
    /// OpenMetrics scrape endpoint.
    [[nodiscard]] auto metrics() noexcept -> metrics::registry&;

    /// Copyable, failure-isolated sink suitable for every cnetmod component
    /// accepting tracing::span_exporter. The sink safely becomes a no-op
    /// after the hub is destroyed.
    [[nodiscard]] auto spans() const -> http::tracing::span_exporter;

    /// Ready-to-install server middleware options sharing this hub's sink.
    [[nodiscard]] auto server_tracing(bool emit_response_traceparent = true,
        bool accept_tracestate = true) const -> http::tracing::tracing_options;

    [[nodiscard]] auto statistics() const noexcept -> otlp_exporter_statistics;
    [[nodiscard]] auto flush(std::chrono::milliseconds timeout =
                                 std::chrono::seconds{5})
        -> task<std::expected<void, std::error_code>>;
    void close() noexcept;

private:
    std::shared_ptr<telemetry_hub_state> state_;
};

} // namespace cnetmod::observability
