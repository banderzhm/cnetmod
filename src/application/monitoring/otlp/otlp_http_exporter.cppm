/**
 * @brief Bounded asynchronous OTLP/HTTP JSON exporter.
 *
 * Completed spans are explicit values. No thread-local trace scope is used,
 * so coroutine migration cannot leak an unrelated trace context.
 */
export module cnetmod.observability.otlp;

import std;
export import cnetmod.instrumentation.metric;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::observability {

/**
 * @brief OTLP endpoints, resource identity, queue, and retry settings.
 */
export struct otlp_http_options
{
    /**
     * @brief Full trace collector endpoint, normally /v1/traces.
     */
    std::string endpoint;
    std::string metrics_endpoint;
    std::string logs_endpoint;
    std::string service_name{"cnetmod"};
    std::string service_version;
    std::string service_namespace;
    std::string service_instance_id;
    std::string deployment_environment;
    std::map<std::string, std::string, std::less<>> resource_attributes;
    /**
     * @brief Collector authentication and tenant headers.
     *
     * Content-Type is managed by the exporter and cannot be overridden.
     */
    std::map<std::string, std::string, std::less<>> headers;
    std::size_t queue_capacity{4096};
    std::size_t max_batch_size{256};
    std::chrono::milliseconds request_timeout{5000};
    std::size_t max_attempts{3};
    std::chrono::milliseconds initial_retry_delay{100};
    std::chrono::milliseconds max_retry_delay{5000};
    /**
     * @brief Selects exported signals independently of endpoint inheritance.
     */
    bool export_traces{true};
    bool export_metrics{true};
    bool export_logs{true};
    /**
     * @brief Forwards framework logger events through the OTLP log pipeline.
     *
     * Disabled by default so adding OTEL does not alter existing logger
     * behavior or add callback work to applications that only need traces or
     * metrics. Explicit operation logs can always use submit_log().
     */
    bool capture_framework_logs{};
    /**
     * @brief Bounds instruments and ordinary attribute sets per instrument.
     *
     * Each instrument may additionally retain one overflow attribute set.
     */
    std::size_t max_metric_instruments{128};
    std::size_t max_metric_attribute_sets{256};
};

export using otel_metric_kind = instrumentation::metric_kind;
export using otel_metric_record = instrumentation::metric_measurement;

/**
 * @brief One structured log record with optional trace correlation.
 */
export struct otel_log_record
{
    std::string severity{"INFO"};
    std::string body;
    std::chrono::system_clock::time_point observed_at =
        std::chrono::system_clock::now();
    std::string trace_id;
    std::string span_id;
    std::vector<std::pair<std::string, std::string>> attributes;
};

/**
 * @brief Thread-safe snapshot of exporter acceptance and delivery counters.
 */
export struct otlp_exporter_statistics
{
    std::uint64_t accepted{};
    std::uint64_t dropped{};
    /**
     * @brief Acknowledged spans, log records, and metric data points on the wire.
     *
     * Cumulative metric snapshots may resend points; this is not a count of
     * producer measurements and must not be subtracted from accepted.
     */
    std::uint64_t exported{};
    std::uint64_t failed_batches{};
    std::uint64_t retries{};
    std::uint64_t accepted_spans{};
    std::uint64_t accepted_metrics{};
    std::uint64_t accepted_logs{};
    std::uint64_t dropped_spans{};
    std::uint64_t dropped_metrics{};
    std::uint64_t dropped_logs{};
    /**
     * @brief Scheduling or execution failures of the background export worker.
     */
    std::uint64_t worker_failures{};
    std::uint64_t rejected_spans{};
    std::uint64_t rejected_metric_points{};
    std::uint64_t rejected_logs{};
    std::uint64_t partial_batches{};
    std::uint64_t warning_batches{};
    std::uint64_t invalid_responses{};
};

class otlp_http_exporter_state;

/**
 * @brief Thread-safe producer backed by bounded lock-free MPMC queues.
 *
 * Delivery runs on the supplied io_context. submit() never performs an HTTP
 * request inline and never blocks an application thread.
 */
export class otlp_http_exporter
{
public:
    otlp_http_exporter(io_context& context, otlp_http_options options);
    ~otlp_http_exporter();
    otlp_http_exporter(const otlp_http_exporter&) = delete;
    auto operator=(const otlp_http_exporter&) -> otlp_http_exporter& = delete;
    otlp_http_exporter(otlp_http_exporter&&) noexcept = default;
    auto operator=(otlp_http_exporter&&) noexcept -> otlp_http_exporter& = default;

    /**
     * @brief Queues a completed span without blocking.
     * @return false when the queue is full or the exporter is closed.
     */
    [[nodiscard]] auto submit(http::tracing::completed_span span) noexcept -> bool;

    /**
     * @brief Queues a metric record without blocking.
     * @return false when disabled, closed, full, or the record is invalid.
     *
     * Names must be nonempty, values finite, and counters nonnegative.
     * Histogram bounds must be finite, strictly increasing, and at most 256.
     * Unsupported metric kinds are rejected. Invalid records do not consume
     * queue capacity and increment the dropped metric count when enabled.
     */
    [[nodiscard]] auto submit(otel_metric_record metric) noexcept -> bool;

    /**
     * @brief Queues a log record without blocking.
     */
    [[nodiscard]] auto submit(otel_log_record record) noexcept -> bool;

    /**
     * @brief Waits until previously accepted records are delivered or failed.
     *
     * A timeout limits this wait only; it does not cancel delivery or close
     * acceptance. The caller must keep the io_context running to finish pending
     * delivery, and may wait again. Success means drained, not lossless delivery;
     * inspect statistics() for failed batches and collector rejections.
     */
    [[nodiscard]] auto flush(std::chrono::milliseconds timeout =
                                 std::chrono::seconds{5})
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto statistics() const noexcept -> otlp_exporter_statistics;
    /**
     * @brief Stops acceptance without cancelling pending delivery.
     *
     * This is not a worker join. Keep the io_context running and await flush()
     * to finish accepted work before destroying the execution context.
     */
    void close() noexcept;

    /**
     * @brief Cancels delivery and discards queued records during forced shutdown.
     *
     * Call only on the exporter's io_context execution thread. This operation
     * is idempotent and permanently closes acceptance. Await flush() afterwards
     * before stopping the context; cancellation is not a synchronous worker join.
     * In-flight batches count as failed and unsent queued records as dropped.
     */
    void abort() noexcept;

    /**
     * @brief Cancels delivery and checks settlement without allocating or waiting.
     * Call on the owning execution thread. False requires further event-loop
     * progress; true means delivery is idle and the connection is closed.
     */
    [[nodiscard]] auto try_settle_shutdown() noexcept -> bool;

    /**
     * @brief Closes acceptance, drains delivery, and settles cancellation.
     *
     * Must run on the exporter's io_context execution thread. The first budget
     * limits graceful delivery; the second limits cancellation settlement.
     * Success guarantees the worker is idle and its connection is closed, not
     * that every record was accepted by the collector. An error does not prove
     * worker settlement; callers must not destroy the execution context merely
     * because this wait expired. Inspect statistics() for delivery failures.
     */
    [[nodiscard]] auto shutdown(std::chrono::milliseconds delivery_timeout,
        std::chrono::milliseconds cancellation_timeout)
        -> task<std::expected<void, std::error_code>>;

private:
    std::shared_ptr<otlp_http_exporter_state> state_;
};

} // namespace cnetmod::observability
