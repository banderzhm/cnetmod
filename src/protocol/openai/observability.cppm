/// cnetmod.protocol.openai:observability — Metrics and distributed tracing

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:observability;

import std;
import cnetmod.protocol.http.middleware.metrics;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.observability.otlp;
import :model;

namespace cnetmod::openai {

export struct model_pricing
{
    double input_per_million_tokens = 0.0;
    double output_per_million_tokens = 0.0;
};

export struct telemetry_options
{
    /// Details can contain prompts, model output or tool arguments and are
    /// excluded by default. Enabling capture requires an application privacy
    /// review.
    bool capture_details = false;
    bool include_name_metric_label = false;
    std::size_t max_attribute_bytes = 1024;
    std::vector<double> duration_buckets = {0.005, 0.01, 0.025, 0.05,
        0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0, 60.0};
    std::map<std::string, model_pricing, std::less<>> pricing;
};

export struct telemetry_statistics
{
    std::uint64_t started = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t unmatched_end_events = 0;
    std::uint64_t dropped_spans = 0;
};

class telemetry_state;

/// Run listener exporting bounded-cardinality OpenMetrics measurements and
/// W3C-correlated spans. Listener failures never affect model execution.
export class telemetry_listener final : public run_listener
{
public:
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
