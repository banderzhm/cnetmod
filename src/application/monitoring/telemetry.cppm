/**
 * @brief Unified SDK-neutral observability composition root.
 *
 * Applications pass lightweight adapters to protocol components while the
 * process owns one resource, bounded exporter, and metric registry.
 */
export module cnetmod.observability;

export import cnetmod.observability.messaging;

import std;
import cnetmod.instrumentation.metric;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability.otlp;
import cnetmod.protocol.http.middleware.metrics;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::observability {

class telemetry_hub_state;

/**
 * @brief Composes traces, metrics, logs, sampling, and shutdown flushing.
 */
export class telemetry_hub
{
public:
    telemetry_hub(io_context& context, otlp_http_options options = {});
    ~telemetry_hub();

    telemetry_hub(const telemetry_hub&) = delete;
    auto operator=(const telemetry_hub&) -> telemetry_hub& = delete;
    telemetry_hub(telemetry_hub&&) noexcept = default;
    auto operator=(telemetry_hub&&) noexcept -> telemetry_hub& = default;

    /**
     * @brief Returns the process metric registry used by adapters and scraping.
     */
    [[nodiscard]] auto metrics() noexcept -> metrics::registry&;
    /**
     * @brief Updates a local counter without allowing telemetry failure to escape.
     * Disabled metrics return before registry lookup or allocation.
     */
    void increment_local_counter(std::string_view name, double value = 1.0) noexcept;
    /**
     * @brief Refreshes local exporter-statistic gauges without exporting them.
     *
     * Disabled local metrics skip all work. Publication failures are isolated
     * per instrument and never interrupt application health supervision.
     * Values are absolute snapshots; repeated refreshes do not add counts.
     */
    void refresh_exporter_metrics() noexcept;

    /**
     * @brief Returns a copyable failure-isolated completed-span sink.
     * Adapter allocation failure returns an empty sink without affecting callers.
     */
    [[nodiscard]] auto spans() const noexcept -> http::tracing::span_exporter;

    /**
     * @brief Returns an independently enabled local/OTLP measurement adapter.
     *
     * The adapter weakly references this hub and is harmless after shutdown.
     * Adapter allocation failure returns an empty sink.
     */
    [[nodiscard]] auto measurements() const noexcept -> instrumentation::metric_sink;

    /**
     * @brief Queues one metric record without blocking the caller.
     */
    [[nodiscard]] auto submit_metric(otel_metric_record metric) noexcept
        -> bool;

    /**
     * @brief Queues one structured log record without blocking the caller.
     */
    [[nodiscard]] auto submit_log(otel_log_record record) noexcept -> bool;

    /**
     * @brief Reports whether an OTLP metric producer can submit records.
     */
    [[nodiscard]] auto exports_metrics() const noexcept -> bool;

    /**
     * @brief Reports whether an OTLP log producer can submit records.
     */
    [[nodiscard]] auto exports_logs() const noexcept -> bool;

    /**
     * @brief Reports whether framework producers should record local metrics.
     */
    [[nodiscard]] auto records_metrics() const noexcept -> bool;

    /**
     * @brief Builds a metric only when export is enabled; isolates factory errors.
     */
    template <typename Factory>
    [[nodiscard]] auto submit_metric_lazy(Factory&& factory) noexcept -> bool
    {
        if (!exports_metrics())
            return false;
        try
        {
            return submit_metric(std::invoke(std::forward<Factory>(factory)));
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * @brief Builds a log only when export is enabled; isolates factory errors.
     */
    template <typename Factory>
    [[nodiscard]] auto submit_log_lazy(Factory&& factory) noexcept -> bool
    {
        if (!exports_logs())
            return false;
        try
        {
            return submit_log(std::invoke(std::forward<Factory>(factory)));
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * @brief Creates HTTP server tracing options sharing this hub's sink.
     */
    [[nodiscard]] auto server_tracing(bool emit_response_traceparent = true,
        bool accept_tracestate = true) const -> http::tracing::tracing_options;

    [[nodiscard]] auto statistics() const noexcept -> otlp_exporter_statistics;

    /**
     * @brief Atomically updates the runtime trace sampling ratio.
     */
    void set_sampling_ratio(double ratio);

    /**
     * @brief Returns the current trace sampling ratio.
     */
    [[nodiscard]] auto sampling_ratio() const noexcept -> double;

    /**
     * @brief Flushes accepted telemetry records within a bounded timeout.
     */
    [[nodiscard]] auto flush(std::chrono::milliseconds timeout =
                                 std::chrono::seconds{5})
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Stops acceptance without waiting for pending delivery.
     */
    void close() noexcept;

    /**
     * @brief Closes producers and requests cancellation without allocating.
     * Call on the Hub execution thread; this is not an active-worker join.
     */
    void abort() noexcept;

    /**
     * @brief Cancels export and checks settlement without allocating or waiting.
     * Call on the Hub execution thread and keep it running while false.
     */
    [[nodiscard]] auto try_settle_shutdown() noexcept -> bool;

    /**
     * @brief Closes producers and settles exporter work on its execution thread.
     *
     * Delivery and cancellation have separate budgets. Success means the worker
     * is idle, not that every record was delivered; inspect statistics() for loss.
     * A failure does not prove settlement and must not be treated as a join.
     */
    [[nodiscard]] auto shutdown(std::chrono::milliseconds delivery_timeout,
        std::chrono::milliseconds cancellation_timeout)
        -> task<std::expected<void, std::error_code>>;

private:
    std::shared_ptr<telemetry_hub_state> state_;
};

} // namespace cnetmod::observability
