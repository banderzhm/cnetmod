module cnetmod.observability;

import std;
import cnetmod.instrumentation.metric;
import cnetmod.instrumentation.tracing;
import cnetmod.core.log;
import cnetmod.utils.flat_map;

namespace cnetmod::observability {

class telemetry_hub_state
{
public:
    telemetry_hub_state(io_context& context, otlp_http_options options)
        : tracing_enabled(options.export_traces && !options.endpoint.empty()),
          metric_export_enabled(options.export_metrics &&
              (!options.metrics_endpoint.empty() || !options.endpoint.empty())),
          log_export_enabled(options.export_logs &&
              (!options.logs_endpoint.empty() || !options.endpoint.empty())),
          capture_framework_logs(options.capture_framework_logs),
          local_metrics_enabled(options.export_metrics),
          exporter(context, std::move(options))
    {
    }

    metrics::registry metric_registry;
    const bool tracing_enabled;
    const bool metric_export_enabled;
    const bool log_export_enabled;
    const bool capture_framework_logs;
    const bool local_metrics_enabled;
    std::atomic<bool> accepting{true};
    std::atomic<logger::observer_id> logger_observer{};
    otlp_http_exporter exporter;
    std::atomic<double> sampling_ratio{1.0};
};

auto otel_severity(logger::level level) noexcept -> std::string_view
{
    switch (level)
    {
    case logger::level::trace:
        return "TRACE";
    case logger::level::debug:
        return "DEBUG";
    case logger::level::info:
        return "INFO";
    case logger::level::warn:
        return "WARN";
    case logger::level::error:
        return "ERROR";
    case logger::level::critical:
        return "FATAL";
    case logger::level::off:
        return "UNSPECIFIED";
    }
    return "UNSPECIFIED";
}

void remove_logger_observer(const std::shared_ptr<telemetry_hub_state>& state) noexcept
{
    if (!state)
        return;
    const auto identifier = state->logger_observer.exchange(0,
        std::memory_order_acq_rel);
    if (identifier != 0)
        logger::remove_observer(identifier);
}

telemetry_hub::telemetry_hub(io_context& context, otlp_http_options options)
    : state_(std::make_shared<telemetry_hub_state>(context, std::move(options)))
{
    if (!state_->log_export_enabled || !state_->capture_framework_logs)
        return;
    try
    {
        const std::weak_ptr<telemetry_hub_state> weak = state_;
        const auto identifier = logger::add_observer([weak](const logger::log_record& event)
            {
                const auto state = weak.lock();
                if (!state || !state->accepting.load(std::memory_order_relaxed))
                    return;
                try
                {
                    otel_log_record record{
                        .severity = std::string{otel_severity(event.severity)},
                        .body = std::string{event.message},
                        .observed_at = event.observed_at,
                        .trace_id = std::string{event.trace_id},
                        .span_id = std::string{event.span_id}};
                    record.attributes.emplace_back("thread.id", event.thread_id);
                    if (!event.source.empty())
                        record.attributes.emplace_back("code.location", event.source);
                    (void)state->exporter.submit(std::move(record));
                }
                catch (...)
                {
                    // Log export must never impair the framework logger.
                }
            });
        state_->logger_observer.store(identifier, std::memory_order_release);
    }
    catch (...)
    {
        // An optional OTLP log bridge must not prevent application startup.
    }
}

telemetry_hub::~telemetry_hub()
{
    close();
}

auto telemetry_hub::metrics() noexcept -> metrics::registry&
{
    return state_->metric_registry;
}

void telemetry_hub::increment_local_counter(std::string_view name, double value) noexcept
{
    if (!records_metrics())
        return;
    try
    {
        metrics().counter_add(name, value);
    }
    catch (...)
    {
        // Local instrumentation must not change application lifecycle outcomes.
    }
}

void telemetry_hub::refresh_exporter_metrics() noexcept
{
    if (!records_metrics())
        return;
    using counter = std::uint64_t otlp_exporter_statistics::*;
    static constexpr std::array<std::pair<std::string_view, counter>, 18> instruments{{
        {"otel_exporter_accepted_total", &otlp_exporter_statistics::accepted},
        {"otel_exporter_dropped_total", &otlp_exporter_statistics::dropped},
        {"otel_exporter_exported_records_total", &otlp_exporter_statistics::exported},
        {"otel_exporter_failed_batches_total", &otlp_exporter_statistics::failed_batches},
        {"otel_exporter_retries_total", &otlp_exporter_statistics::retries},
        {"otel_exporter_accepted_spans_total", &otlp_exporter_statistics::accepted_spans},
        {"otel_exporter_accepted_metrics_total", &otlp_exporter_statistics::accepted_metrics},
        {"otel_exporter_accepted_logs_total", &otlp_exporter_statistics::accepted_logs},
        {"otel_exporter_dropped_spans_total", &otlp_exporter_statistics::dropped_spans},
        {"otel_exporter_dropped_metrics_total", &otlp_exporter_statistics::dropped_metrics},
        {"otel_exporter_dropped_logs_total", &otlp_exporter_statistics::dropped_logs},
        {"otel_exporter_worker_failures_total", &otlp_exporter_statistics::worker_failures},
        {"otel_exporter_rejected_spans_total", &otlp_exporter_statistics::rejected_spans},
        {"otel_exporter_rejected_metric_points_total", &otlp_exporter_statistics::rejected_metric_points},
        {"otel_exporter_rejected_logs_total", &otlp_exporter_statistics::rejected_logs},
        {"otel_exporter_partial_batches_total", &otlp_exporter_statistics::partial_batches},
        {"otel_exporter_warning_batches_total", &otlp_exporter_statistics::warning_batches},
        {"otel_exporter_invalid_responses_total", &otlp_exporter_statistics::invalid_responses},
    }};
    const auto snapshot = statistics();
    for (const auto& [name, member] : instruments)
    {
        try
        {
            state_->metric_registry.gauge_set(name, static_cast<double>(snapshot.*member));
        }
        catch (...)
        {
            // One unavailable metric must not stop the remaining publications.
        }
    }
}

auto telemetry_hub::spans() const noexcept -> http::tracing::span_exporter
try
{
    if (!state_ || !state_->tracing_enabled ||
        !state_->accepting.load(std::memory_order_relaxed))
        return {};
    const std::weak_ptr<telemetry_hub_state> weak = state_;
    return {[weak](const http::tracing::completed_span& span)
        {
            if (const auto state = weak.lock())
            {
                if (state->accepting.load(std::memory_order_relaxed) && (span.context.flags & 1U) != 0)
                    (void)state->exporter.submit(span);
            }
        },
        [weak](const instrumentation::trace_context& context)
        {
            if (const auto state = weak.lock())
            {
                const auto ratio = state->sampling_ratio.load(
                    std::memory_order_relaxed);
                if (ratio <= 0.0)
                    return false;
                if (ratio >= 1.0)
                    return true;
                if (context.trace_id.size() != 32U)
                    return false;
                std::uint64_t random_bits = 0;
                const auto* begin = context.trace_id.data() + 16;
                const auto parsed = std::from_chars(begin, begin + 16, random_bits, 16);
                return parsed.ec == std::errc{} && parsed.ptr == begin + 16 &&
                    static_cast<double>(random_bits >> 11U) / 9007199254740992.0 < ratio;
            }
            return false;
        }};
}
catch (...)
{
    return {};
}

auto telemetry_hub::server_tracing(bool emit_response_traceparent,
    bool accept_tracestate) const -> http::tracing::tracing_options
{
    return {
        .emit_response_traceparent = emit_response_traceparent,
        .accept_tracestate = accept_tracestate,
        .on_end = spans(),
    };
}

auto telemetry_hub::measurements() const noexcept -> instrumentation::metric_sink
try
{
    if (!state_ || (!state_->local_metrics_enabled && !state_->metric_export_enabled) ||
        !state_->accepting.load(std::memory_order_relaxed))
        return {};
    const std::weak_ptr<telemetry_hub_state> weak = state_;
    return [weak](instrumentation::metric_measurement measurement)
    {
        const auto state = weak.lock();
        if (!state || !state->accepting.load(std::memory_order_relaxed) ||
            !instrumentation::valid_metric_measurement(measurement))
            return;
        if (state->local_metrics_enabled)
        {
            try
            {
                auto name = measurement.name;
                std::ranges::replace(name, '.', '_');
                if (measurement.unit == "s")
                    name += "_seconds";
                if (measurement.kind == instrumentation::metric_kind::counter)
                    name += "_total";
                metrics::labels labels;
                for (const auto& [key, value] : measurement.attributes)
                {
                    auto label = key;
                    std::ranges::replace(label, '.', '_');
                    labels.insert_or_assign(std::move(label), value);
                }
                switch (measurement.kind)
                {
                case instrumentation::metric_kind::counter:
                    state->metric_registry.counter_add(name, measurement.value, std::move(labels));
                    break;
                case instrumentation::metric_kind::gauge:
                    state->metric_registry.gauge_set(name, measurement.value, std::move(labels));
                    break;
                case instrumentation::metric_kind::histogram:
                    state->metric_registry.histogram_observe(name, measurement.value,
                        measurement.explicit_bounds, std::move(labels));
                    break;
                }
            }
            catch (...)
            {
                // A local registry failure must not suppress OTLP submission.
            }
        }
        if (state->metric_export_enabled)
            (void)state->exporter.submit(std::move(measurement));
    };
}
catch (...)
{
    return {};
}

auto telemetry_hub::submit_metric(otel_metric_record metric) noexcept -> bool
{
    return state_ && state_->exporter.submit(std::move(metric));
}

auto telemetry_hub::submit_log(otel_log_record record) noexcept -> bool
{
    return state_ && state_->exporter.submit(std::move(record));
}

auto telemetry_hub::statistics() const noexcept -> otlp_exporter_statistics
{
    return state_ ? state_->exporter.statistics() : otlp_exporter_statistics{};
}

auto telemetry_hub::exports_metrics() const noexcept -> bool
{
    return state_ && state_->metric_export_enabled &&
        state_->accepting.load(std::memory_order_relaxed);
}

auto telemetry_hub::exports_logs() const noexcept -> bool
{
    return state_ && state_->log_export_enabled &&
        state_->accepting.load(std::memory_order_relaxed);
}

auto telemetry_hub::records_metrics() const noexcept -> bool
{
    return state_ && state_->local_metrics_enabled &&
        state_->accepting.load(std::memory_order_relaxed);
}

void telemetry_hub::set_sampling_ratio(double ratio)
{
    if (!std::isfinite(ratio) || ratio < 0.0 || ratio > 1.0)
        throw std::invalid_argument("sampling ratio must be between zero and one");
    state_->sampling_ratio.store(ratio, std::memory_order_relaxed);
}

auto telemetry_hub::sampling_ratio() const noexcept -> double
{
    return state_ ? state_->sampling_ratio.load(std::memory_order_relaxed)
                  : 0.0;
}

auto telemetry_hub::flush(std::chrono::milliseconds timeout)
    -> task<std::expected<void, std::error_code>>
{
    const auto state = state_;
    if (!state)
        co_return {};
    co_return co_await state->exporter.flush(timeout);
}

void telemetry_hub::close() noexcept
{
    if (state_)
    {
        state_->accepting.store(false, std::memory_order_relaxed);
        remove_logger_observer(state_);
        state_->exporter.close();
    }
}

auto telemetry_hub::shutdown(std::chrono::milliseconds delivery_timeout,
    std::chrono::milliseconds cancellation_timeout)
    -> task<std::expected<void, std::error_code>>
{
    const auto state = state_;
    if (!state)
        co_return {};
    state->accepting.store(false, std::memory_order_relaxed);
    remove_logger_observer(state);
    try
    {
        co_return co_await state->exporter.shutdown(delivery_timeout, cancellation_timeout);
    }
    catch (const std::bad_alloc&)
    {
        state->exporter.abort();
        co_return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        state->exporter.abort();
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }
}

void telemetry_hub::abort() noexcept
{
    close();
    if (state_)
        state_->exporter.abort();
}

auto telemetry_hub::try_settle_shutdown() noexcept -> bool
{
    close();
    return !state_ || state_->exporter.try_settle_shutdown();
}

} // namespace cnetmod::observability
