/// cnetmod.protocol.openai:observability — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.protocol.http.middleware.metrics;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.observability.otlp;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :model;
import :observability;

namespace cnetmod::openai {

namespace {

    using span_submission =
        std::function<bool(http::tracing::completed_span)>;

    struct event_kind
    {
        std::string_view operation;
        bool start = false;
        bool end = false;
        bool failed = false;
    };

    auto classify(run_event_type type) noexcept -> event_kind
    {
        switch (type)
        {
        case run_event_type::model_start:
            return {"chat", true, false, false};
        case run_event_type::model_end:
            return {"chat", false, true, false};
        case run_event_type::model_error:
            return {"chat", false, true, true};
        case run_event_type::model_retry:
            return {"chat.retry", false, false, false};
        case run_event_type::model_rejected:
            return {"chat.rejected", false, false, true};
        case run_event_type::tool_start:
            return {"tool", true, false, false};
        case run_event_type::tool_end:
            return {"tool", false, true, false};
        case run_event_type::tool_error:
            return {"tool", false, true, true};
        case run_event_type::retriever_start:
            return {"retrieval", true, false, false};
        case run_event_type::retriever_end:
            return {"retrieval", false, true, false};
        case run_event_type::retriever_error:
            return {"retrieval", false, true, true};
        case run_event_type::agent_start:
            return {"agent", true, false, false};
        case run_event_type::agent_end:
            return {"agent", false, true, false};
        case run_event_type::agent_error:
            return {"agent", false, true, true};
        }
        return {};
    }

    auto bounded(std::string value, std::size_t limit) -> std::string
    {
        if (value.size() <= limit)
            return value;
        value.resize(limit);
        return value;
    }

    auto attribute_text(const json& value, std::size_t limit) -> std::string
    {
        if (value.is_string())
            return bounded(value.get<std::string>(), limit);
        return bounded(value.dump(), limit);
    }

    auto operation_key(const run_event& event, std::string_view operation)
        -> std::string
    {
        std::string identifier;
        if (const auto found = event.attributes.find("operation_id");
            found != event.attributes.end() && found->is_string())
            identifier = found->get<std::string>();
        return std::format("{}\x1f{}\x1f{}", event.run_id,
            operation, identifier);
    }

    auto operation_identifier(const run_event& event) -> std::string
    {
        if (const auto found = event.attributes.find("operation_id");
            found != event.attributes.end() && found->is_string())
            return found->get<std::string>();
        return {};
    }

    auto metric_labels(const run_event& event, std::string_view operation,
        std::string_view status, bool include_name) -> metrics::labels
    {
        metrics::labels labels{{"operation", std::string{operation}}};
        if (!status.empty())
            labels.insert_or_assign("status", std::string{status});
        if (include_name && !event.name.empty())
            labels.insert_or_assign("name", event.name);
        return labels;
    }

    auto number_attribute(const run_event& event, std::string_view name)
        -> double
    {
        const auto found = event.attributes.find(name);
        return found != event.attributes.end() && found->is_number()
            ? found->get<double>()
            : 0.0;
    }

} // namespace

class telemetry_state
{
public:
    telemetry_state(metrics::registry& metric_registry,
        span_submission exporter, telemetry_options configuration)
        : metric_registry(metric_registry), exporter(std::move(exporter)), options(std::move(configuration))
    {
        if (options.max_attribute_bytes == 0)
            throw std::invalid_argument(
                "telemetry max_attribute_bytes must be greater than zero");
        if (options.duration_buckets.empty() ||
            !std::ranges::is_sorted(options.duration_buckets) ||
            std::ranges::any_of(options.duration_buckets,
                [](double value)
                {
                    return !std::isfinite(value) || value <= 0;
                }))
            throw std::invalid_argument(
                "telemetry duration buckets must be finite, positive and sorted");
    }

    void observe(const run_event& event)
    {
        const auto kind = classify(event.type);
        if (kind.operation.empty())
            return;
        if (!kind.start && !kind.end)
        {
            observe_instant(event, kind);
            return;
        }
        if (kind.start)
            begin(event, kind);
        else
            finish(event, kind);
    }

    auto snapshot() const noexcept -> telemetry_statistics
    {
        return {.started = started.load(std::memory_order_relaxed),
            .completed = completed.load(std::memory_order_relaxed),
            .failed = failed.load(std::memory_order_relaxed),
            .unmatched_end_events = unmatched.load(std::memory_order_relaxed),
            .dropped_spans = dropped.load(std::memory_order_relaxed)};
    }

private:
    struct active_operation
    {
        std::chrono::steady_clock::time_point started;
        std::chrono::system_clock::time_point started_at;
        http::tracing::trace_context context;
        std::string parent_span_id;
        std::string operation_id;
        std::string name;
        std::string run_id;
        std::string detail;
        json attributes;
    };

    struct trace_entry
    {
        http::tracing::trace_context context;
        std::size_t active_operations = 0;
    };

    void observe_instant(const run_event& event, const event_kind& kind)
    {
        const auto labels = metric_labels(event, kind.operation, {},
            options.include_name_metric_label);
        if (event.type == run_event_type::model_retry)
            metric_registry.counter_add("gen_ai_client_retries_total", 1.0,
                labels, "Generative AI model retry attempts.");
        else
            metric_registry.counter_add("gen_ai_client_rejections_total", 1.0,
                labels, "Generative AI requests rejected by local governance.");
    }

    void begin(const run_event& event, const event_kind& kind)
    {
        std::size_t active_count = 0;
        {
            concurrent_containers::exclusive_latch_guard lock{latch};
            auto& trace = traces[event.run_id];
            if (trace.active_operations == 0)
                trace.context = event.trace_parent.value_or(
                    http::tracing::new_root_context());
            ++trace.active_operations;
            auto parent_context = trace.context;
            if (!event.parent_operation_id.empty())
            {
                if (const auto parent = operation_contexts.find(
                        event.parent_operation_id);
                    parent != operation_contexts.end())
                    parent_context = parent->second;
            }
            auto context = http::tracing::child_context(parent_context);
            const auto operation_id = operation_identifier(event);
            if (!operation_id.empty())
                operation_contexts.insert_or_assign(operation_id, context);
            active[operation_key(event, kind.operation)].push_back(
                {.started = std::chrono::steady_clock::now(),
                    .started_at = std::chrono::system_clock::now(),
                    .context = std::move(context),
                    .parent_span_id = parent_context.span_id,
                    .operation_id = operation_id,
                    .name = event.name,
                    .run_id = event.run_id,
                    .detail = options.capture_details
                        ? bounded(event.detail, options.max_attribute_bytes)
                        : std::string{},
                    .attributes = event.attributes});
            active_count = ++active_by_kind[std::string{kind.operation}];
        }
        started.fetch_add(1, std::memory_order_relaxed);
        metric_registry.counter_add("gen_ai_client_operations_total", 1.0,
            metric_labels(event, kind.operation, "started",
                options.include_name_metric_label),
            "Generative AI operations by lifecycle status.");
        metric_registry.gauge_set("gen_ai_client_active_operations",
            static_cast<double>(active_count),
            metric_labels(event, kind.operation, {}, false),
            "Generative AI operations currently in progress.");
    }

    void finish(const run_event& event, const event_kind& kind)
    {
        std::optional<active_operation> operation;
        std::size_t active_count = 0;
        {
            concurrent_containers::exclusive_latch_guard lock{latch};
            const auto key = operation_key(event, kind.operation);
            const auto found = active.find(key);
            if (found != active.end() && !found->second.empty())
            {
                operation = std::move(found->second.back());
                found->second.pop_back();
                if (found->second.empty())
                    active.erase(found);
                if (!operation->operation_id.empty())
                    operation_contexts.erase(operation->operation_id);
                auto count = active_by_kind.find(kind.operation);
                if (count != active_by_kind.end() && count->second > 0)
                {
                    --count->second;
                    active_count = count->second;
                }
                const auto trace = traces.find(event.run_id);
                if (trace != traces.end() && trace->second.active_operations > 0)
                {
                    --trace->second.active_operations;
                    if (trace->second.active_operations == 0)
                        traces.erase(trace);
                }
            }
        }
        if (!operation)
        {
            unmatched.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto elapsed = std::chrono::steady_clock::now() - operation->started;
        const auto seconds = std::chrono::duration<double>(elapsed).count();
        completed.fetch_add(1, std::memory_order_relaxed);
        if (kind.failed)
            failed.fetch_add(1, std::memory_order_relaxed);
        const auto status = kind.failed ? "error" : "success";
        metric_registry.counter_add("gen_ai_client_operations_total", 1.0,
            metric_labels(event, kind.operation, status,
                options.include_name_metric_label),
            "Generative AI operations by lifecycle status.");
        metric_registry.gauge_set("gen_ai_client_active_operations",
            static_cast<double>(active_count),
            metric_labels(event, kind.operation, {}, false),
            "Generative AI operations currently in progress.");
        metric_registry.histogram_observe("gen_ai_client_operation_duration_seconds",
            seconds, options.duration_buckets,
            metric_labels(event, kind.operation, status,
                options.include_name_metric_label),
            "Generative AI operation duration in seconds.");
        record_usage(event, kind.operation);
        export_span(event, kind, std::move(*operation), elapsed);
    }

    void record_usage(const run_event& event, std::string_view operation)
    {
        if (operation != "chat")
            return;
        const auto input = number_attribute(event, "input_tokens");
        const auto output = number_attribute(event, "output_tokens");
        auto labels = metric_labels(event, operation, {},
            options.include_name_metric_label);
        if (input > 0)
        {
            labels.insert_or_assign("token_type", "input");
            metric_registry.counter_add("gen_ai_client_tokens_total", input,
                labels, "Generative AI tokens consumed by direction.");
        }
        if (output > 0)
        {
            labels.insert_or_assign("token_type", "output");
            metric_registry.counter_add("gen_ai_client_tokens_total", output,
                labels, "Generative AI tokens consumed by direction.");
        }
        const auto price = options.pricing.find(event.name);
        if (price != options.pricing.end())
        {
            const auto cost = (input * price->second.input_per_million_tokens +
                                  output * price->second.output_per_million_tokens) /
                1'000'000.0;
            metric_registry.counter_add("gen_ai_client_cost_total", cost,
                metric_labels(event, operation, {},
                    options.include_name_metric_label),
                "Estimated generative AI model cost in configured currency.");
        }
    }

    void export_span(const run_event& event, const event_kind& kind,
        active_operation operation, std::chrono::steady_clock::duration elapsed)
    {
        if (!exporter)
            return;
        std::vector<std::pair<std::string, std::string>> attributes{
            {"gen_ai.operation.name", std::string{kind.operation}},
            {"gen_ai.run.id", bounded(operation.run_id, options.max_attribute_bytes)}};
        if (!event.name.empty())
            attributes.emplace_back("gen_ai.request.model_or_name",
                bounded(event.name, options.max_attribute_bytes));
        for (auto attribute = event.attributes.cbegin();
            attribute != event.attributes.cend(); ++attribute)
            attributes.emplace_back("gen_ai." + attribute.key(),
                attribute_text(attribute.value(), options.max_attribute_bytes));
        if (options.capture_details)
        {
            if (!operation.detail.empty())
                attributes.emplace_back("gen_ai.request.detail",
                    std::move(operation.detail));
            if (!event.detail.empty())
                attributes.emplace_back("gen_ai.response.detail",
                    bounded(event.detail, options.max_attribute_bytes));
        }
        try
        {
            const auto accepted = exporter({.context = std::move(operation.context),
                .name = std::format("gen_ai.{} {}", kind.operation,
                    event.name),
                .elapsed = elapsed,
                .failed = kind.failed,
                .attributes = std::move(attributes),
                .parent_span_id = std::move(operation.parent_span_id),
                .started_at = operation.started_at,
                .ended_at = std::chrono::system_clock::now(),
                .kind = http::tracing::span_kind::client});
            if (!accepted)
            {
                dropped.fetch_add(1, std::memory_order_relaxed);
                metric_registry.counter_add("gen_ai_client_spans_dropped_total",
                    1.0, {}, "Generative AI spans rejected by the exporter.");
            }
        }
        catch (...)
        {
            dropped.fetch_add(1, std::memory_order_relaxed);
            metric_registry.counter_add("gen_ai_client_spans_dropped_total",
                1.0, {}, "Generative AI spans rejected by the exporter.");
        }
    }

public:
    metrics::registry& metric_registry;
    span_submission exporter;
    telemetry_options options;
    mutable concurrent_containers::atomic_rw_latch latch;
    std::map<std::string, std::vector<active_operation>, std::less<>> active;
    std::map<std::string, trace_entry, std::less<>> traces;
    std::map<std::string, http::tracing::trace_context, std::less<>>
        operation_contexts;
    std::map<std::string, std::size_t, std::less<>> active_by_kind;
    std::atomic<std::uint64_t> started{0};
    std::atomic<std::uint64_t> completed{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> unmatched{0};
    std::atomic<std::uint64_t> dropped{0};
};

telemetry_listener::telemetry_listener(metrics::registry& metric_registry,
    http::tracing::span_exporter span_exporter, telemetry_options options)
    : state_(std::make_shared<telemetry_state>(metric_registry,
          span_exporter
              ? span_submission{[exporter = std::move(span_exporter)](
                                    http::tracing::completed_span span)
                    {
                        exporter(span);
                        return true;
                    }}
              : span_submission{},
          std::move(options)))
{
}

telemetry_listener::telemetry_listener(metrics::registry& metric_registry,
    observability::otlp_http_exporter& exporter, telemetry_options options)
    : state_(std::make_shared<telemetry_state>(metric_registry, [&exporter](http::tracing::completed_span span)
          {
              return exporter.submit(std::move(span));
          },
          std::move(options)))
{
}

telemetry_listener::~telemetry_listener() = default;
telemetry_listener::telemetry_listener(telemetry_listener&&) noexcept = default;
auto telemetry_listener::operator=(telemetry_listener&&) noexcept
    -> telemetry_listener& = default;

void telemetry_listener::on_event(const run_event& event)
{
    state_->observe(event);
}

auto telemetry_listener::statistics() const noexcept -> telemetry_statistics
{
    return state_->snapshot();
}

} // namespace cnetmod::openai
