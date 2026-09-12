module cnetmod.observability;

import std;

namespace cnetmod::observability {

class telemetry_hub_state
{
public:
    telemetry_hub_state(io_context& context, otlp_http_options options)
        : exporter(context, std::move(options))
    {
    }

    metrics::registry metric_registry;
    otlp_http_exporter exporter;
};

telemetry_hub::telemetry_hub(io_context& context, otlp_http_options options)
    : state_(std::make_shared<telemetry_hub_state>(context, std::move(options)))
{
}

telemetry_hub::~telemetry_hub()
{
    close();
}

auto telemetry_hub::metrics() noexcept -> metrics::registry&
{
    return state_->metric_registry;
}

auto telemetry_hub::spans() const -> http::tracing::span_exporter
{
    const std::weak_ptr<telemetry_hub_state> weak = state_;
    return [weak](const http::tracing::completed_span& span)
    {
        if (const auto state = weak.lock())
            (void)state->exporter.submit(span);
    };
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

auto telemetry_hub::statistics() const noexcept -> otlp_exporter_statistics
{
    return state_ ? state_->exporter.statistics() : otlp_exporter_statistics{};
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
        state_->exporter.close();
}

} // namespace cnetmod::observability
