/**
 * @brief Optional adapter from OpenAI run events to metrics and exported spans.
 */

module;

#include <cnetmod/config.hpp>

export module cnetmod.observability.openai;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import std;
import cnetmod.instrumentation.metric;
import cnetmod.protocol.http.middleware.metrics;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.observability.otlp;
import cnetmod.protocol.openai;

namespace cnetmod::openai {

export struct model_pricing
{
    double input_per_million_tokens = 0.0;
    double output_per_million_tokens = 0.0;
};

export struct telemetry_options
{
    /**
     * @brief Opts into request/response detail capture after a privacy review.
     *
     * Run metadata, tags and arbitrary attributes remain excluded regardless
     * of this setting. Only recognized usage, model and streaming fields are
     * automatically exported from event attributes.
     */
    bool capture_details = false;
    bool include_name_metric_label = false;
    std::size_t max_attribute_bytes = 1024;
    std::vector<double> duration_buckets = {0.005, 0.01, 0.025, 0.05,
        0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0, 60.0};
    std::map<std::string, model_pricing, std::less<>> pricing;
    /**
     * @brief Enables local metric aggregation independently of span export.
     */
    bool record_metrics = true;
};

export struct telemetry_statistics
{
    std::uint64_t started = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t unmatched_end_events = 0;
    std::uint64_t dropped_spans = 0;
    /**
     * @brief Counts failed metric publication batches without suppressing traces.
     */
    std::uint64_t metric_failures = 0;
};

class telemetry_state;

/**
 * @brief Adapts run events to OpenMetrics measurements and W3C-correlated spans.
 *
 * Trace identities are created only when a span exporter is installed. A valid
 * explicit trace parent or an active parent operation in the same run supplies
 * ancestry; otherwise the operation itself is a root span. Run IDs alone do
 * not imply parentage between concurrent operations.
 */
export class telemetry_listener final : public run_listener
{
public:
    /**
     * @brief Publishes GenAI measurements through an SDK-neutral metric sink.
     *
     * A hub measurement sink provides local aggregation and OTLP delivery without
     * duplicate recording. The sink must be nonblocking and safe for concurrent calls.
     * With no span sink and no enabled metric destination, events are ignored
     * before operation tracking; listener statistics remain zero.
     */
    telemetry_listener(instrumentation::metric_sink measurements,
        http::tracing::span_exporter span_exporter,
        telemetry_options options = {});
    telemetry_listener(metrics::registry& metric_registry,
        http::tracing::span_exporter span_exporter = {},
        telemetry_options options = {});
    telemetry_listener(metrics::registry& metric_registry,
        observability::otlp_http_exporter& exporter,
        telemetry_options options = {});
    ~telemetry_listener();

    telemetry_listener(const telemetry_listener&) = delete;
    auto operator=(const telemetry_listener&) -> telemetry_listener& = delete;
    telemetry_listener(telemetry_listener&&) noexcept;
    auto operator=(telemetry_listener&&) noexcept -> telemetry_listener&;

    void on_event(const run_event& event) override;
    [[nodiscard]] auto statistics() const noexcept -> telemetry_statistics;

private:
    std::shared_ptr<telemetry_state> state_;
};

} // namespace cnetmod::openai
#endif
