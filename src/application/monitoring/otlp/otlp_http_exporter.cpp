module cnetmod.observability.otlp;

import std;
import cnetmod.observability.export_response;
import cnetmod.observability.export_retry;
import cnetmod.instrumentation.metric_aggregation;
import cnetmod.instrumentation.operation_result;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.http;
import cnetmod.utils.concurrent_containers.queue;

namespace cnetmod::observability {
namespace {

    auto operation_status_name(instrumentation::operation_status status) noexcept
        -> std::string_view
    {
        switch (status)
        {
        case instrumentation::operation_status::success:
            return "success";
        case instrumentation::operation_status::error:
            return "error";
        case instrumentation::operation_status::cancelled:
            return "cancelled";
        case instrumentation::operation_status::timeout:
            return "timeout";
        case instrumentation::operation_status::abandoned:
            return "abandoned";
        }
        return "unknown";
    }

    auto append_json_string(std::string& out, std::string_view value) -> void
    {
        out.push_back('"');
        for (const auto character : value)
        {
            switch (character)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(character) < 0x20U)
                    out += std::format("\\u{:04x}",
                        static_cast<unsigned char>(character));
                else
                    out.push_back(character);
            }
        }
        out.push_back('"');
    }

    template <class Duration>
    auto unix_nanoseconds(
        std::chrono::time_point<std::chrono::system_clock, Duration> value)
        -> std::string
    {
        return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
            value.time_since_epoch())
                .count());
    }

    auto span_kind_name(http::tracing::span_kind kind) -> std::string_view
    {
        using enum http::tracing::span_kind;
        switch (kind)
        {
        case unspecified:
            return "SPAN_KIND_UNSPECIFIED";
        case server:
            return "SPAN_KIND_SERVER";
        case client:
            return "SPAN_KIND_CLIENT";
        case producer:
            return "SPAN_KIND_PRODUCER";
        case consumer:
            return "SPAN_KIND_CONSUMER";
        case internal:
            return "SPAN_KIND_INTERNAL";
        }
        return "SPAN_KIND_UNSPECIFIED";
    }

    auto append_string_attribute(std::string& result, bool& first,
        std::string_view key, std::string_view value) -> void
    {
        if (!first)
            result.push_back(',');
        first = false;
        result += "{\"key\":";
        append_json_string(result, key);
        result += ",\"value\":{\"stringValue\":";
        append_json_string(result, value);
        result += "}}";
    }

    auto encode_batch(const otlp_http_options& options,
        const std::vector<http::tracing::completed_span>& spans) -> std::string
    {
        std::string result{"{\"resourceSpans\":[{\"resource\":{\"attributes\":["};
        bool first_resource = true;
        append_string_attribute(result, first_resource, "service.name",
            options.service_name);
        if (!options.service_version.empty())
            append_string_attribute(result, first_resource, "service.version",
                options.service_version);
        if (!options.service_namespace.empty())
            append_string_attribute(result, first_resource, "service.namespace",
                options.service_namespace);
        if (!options.service_instance_id.empty())
            append_string_attribute(result, first_resource, "service.instance.id",
                options.service_instance_id);
        if (!options.deployment_environment.empty())
            append_string_attribute(result, first_resource,
                "deployment.environment.name", options.deployment_environment);
        for (const auto& [key, value] : options.resource_attributes)
            append_string_attribute(result, first_resource, key, value);
        result += "]},\"scopeSpans\":[{\"scope\":{\"name\":\"cnetmod\",\"version\":\"1\"},\"spans\":[";
        for (std::size_t index{}; index < spans.size(); ++index)
        {
            if (index != 0U)
                result.push_back(',');
            const auto& span = spans[index];
            const auto fallback_end = std::chrono::system_clock::now();
            const auto ended = span.ended_at.time_since_epoch().count() == 0
                ? fallback_end
                : span.ended_at;
            const auto started = span.started_at.time_since_epoch().count() == 0
                ? ended - std::chrono::duration_cast<std::chrono::system_clock::duration>(span.elapsed)
                : span.started_at;
            result += "{\"traceId\":";
            append_json_string(result, span.context.trace_id);
            result += ",\"spanId\":";
            append_json_string(result, span.context.span_id);
            if (!span.parent_span_id.empty())
            {
                result += ",\"parentSpanId\":";
                append_json_string(result, span.parent_span_id);
            }
            if (!span.context.tracestate.empty())
            {
                result += ",\"traceState\":";
                append_json_string(result, span.context.tracestate);
            }
            result += ",\"name\":";
            const auto name = span.name.empty() ? span.method + " " + span.path : span.name;
            append_json_string(result, name);
            result += ",\"kind\":\"";
            const auto kind = span.kind == http::tracing::span_kind::unspecified
                ? (span.name.empty() ? http::tracing::span_kind::server
                                     : http::tracing::span_kind::client)
                : span.kind;
            result += span_kind_name(kind);
            result += "\",\"startTimeUnixNano\":";
            append_json_string(result, unix_nanoseconds(started));
            result += ",\"endTimeUnixNano\":";
            append_json_string(result, unix_nanoseconds(ended));
            result += ",\"attributes\":[";
            bool first_attribute = true;
            const auto append_attribute = [&](std::string_view key, std::string_view value)
            {
                append_string_attribute(result, first_attribute, key, value);
            };
            if (!span.method.empty())
            {
                append_attribute("http.request.method", span.method);
                append_attribute("url.path", span.path);
                if (span.status_code > 0)
                {
                    if (!first_attribute)
                        result.push_back(',');
                    first_attribute = false;
                    result += "{\"key\":\"http.response.status_code\",\"value\":{\"intValue\":" +
                        std::to_string(span.status_code) + "}}";
                }
            }
            for (const auto& [key, value] : span.attributes)
                append_attribute(key, value);
            const auto outcome = span.failed &&
                    span.result.status == instrumentation::operation_status::success
                ? instrumentation::operation_status::error
                : span.result.status;
            append_attribute("cnetmod.operation.status", operation_status_name(outcome));
            if (span.result.error)
            {
                append_attribute("cnetmod.error.category", span.result.error.category().name());
                append_attribute("cnetmod.error.code", std::to_string(span.result.error.value()));
            }
            result += "]";
            if (span.failed || outcome == instrumentation::operation_status::error ||
                outcome == instrumentation::operation_status::timeout ||
                outcome == instrumentation::operation_status::abandoned)
                result += ",\"status\":{\"code\":\"STATUS_CODE_ERROR\"}";
            result += ",\"flags\":" + std::to_string(span.context.flags);
            result.push_back('}');
        }
        result += "]}]}]}";
        return result;
    }

    auto append_resource(std::string& result,
        const otlp_http_options& options) -> void
    {
        bool first = true;
        append_string_attribute(result, first, "service.name",
            options.service_name);
        if (!options.service_version.empty())
            append_string_attribute(result, first, "service.version",
                options.service_version);
        if (!options.service_namespace.empty())
            append_string_attribute(result, first, "service.namespace",
                options.service_namespace);
        if (!options.service_instance_id.empty())
            append_string_attribute(result, first, "service.instance.id",
                options.service_instance_id);
        if (!options.deployment_environment.empty())
            append_string_attribute(result, first,
                "deployment.environment.name", options.deployment_environment);
        for (const auto& [key, value] : options.resource_attributes)
            append_string_attribute(result, first, key, value);
    }

    auto append_attributes(std::string& result,
        const std::vector<std::pair<std::string, std::string>>& attributes)
        -> void
    {
        bool first = true;
        for (const auto& [key, value] : attributes)
            append_string_attribute(result, first, key, value);
    }

    auto encode_logs(const otlp_http_options& options,
        const std::vector<otel_log_record>& records) -> std::string
    {
        std::string result{"{\"resourceLogs\":[{\"resource\":{\"attributes\":["};
        append_resource(result, options);
        result += "]},\"scopeLogs\":[{\"scope\":{\"name\":\"cnetmod\",\"version\":\"1\"},\"logRecords\":[";
        for (std::size_t index{}; index < records.size(); ++index)
        {
            if (index != 0U)
                result.push_back(',');
            const auto& record = records[index];
            result += "{\"timeUnixNano\":";
            append_json_string(result, unix_nanoseconds(record.observed_at));
            result += ",\"severityText\":";
            append_json_string(result, record.severity);
            result += ",\"body\":{\"stringValue\":";
            append_json_string(result, record.body);
            result += "},\"attributes\":[";
            append_attributes(result, record.attributes);
            result.push_back(']');
            if (!record.trace_id.empty())
            {
                result += ",\"traceId\":";
                append_json_string(result, record.trace_id);
            }
            if (!record.span_id.empty())
            {
                result += ",\"spanId\":";
                append_json_string(result, record.span_id);
            }
            result.push_back('}');
        }
        result += "]}]}]}";
        return result;
    }

    void append_histogram(std::string& result, const instrumentation::metric_point& point,
        const std::vector<double>& bounds)
    {
        result += ",\"count\":";
        append_json_string(result, std::to_string(point.count));
        if (!point.has_negative)
            result += ",\"sum\":" + std::format("{:.{}g}", point.value, std::numeric_limits<double>::max_digits10);
        result += ",\"min\":" + std::format("{:.{}g}", point.minimum, std::numeric_limits<double>::max_digits10);
        result += ",\"max\":" + std::format("{:.{}g}", point.maximum, std::numeric_limits<double>::max_digits10);
        result += ",\"explicitBounds\":[";
        for (std::size_t index{}; index < bounds.size(); ++index)
        {
            if (index != 0U)
                result.push_back(',');
            result += std::format("{:.{}g}", bounds[index], std::numeric_limits<double>::max_digits10);
        }
        result += "],\"bucketCounts\":[";
        for (std::size_t index{}; index < point.bucket_counts.size(); ++index)
        {
            if (index != 0U)
                result.push_back(',');
            append_json_string(result, std::to_string(point.bucket_counts[index]));
        }
        result.push_back(']');
    }

    auto encode_metrics(const otlp_http_options& options,
        const std::vector<instrumentation::metric_series>& records) -> std::string
    {
        std::string result{"{\"resourceMetrics\":[{\"resource\":{\"attributes\":["};
        append_resource(result, options);
        result += "]},\"scopeMetrics\":[{\"scope\":{\"name\":\"cnetmod\",\"version\":\"1\"},\"metrics\":[";
        for (std::size_t index{}; index < records.size(); ++index)
        {
            if (index != 0U)
                result.push_back(',');
            const auto& record = records[index];
            result += "{\"name\":";
            append_json_string(result, record.name);
            if (!record.unit.empty())
            {
                result += ",\"unit\":";
                append_json_string(result, record.unit);
            }
            result += record.kind == otel_metric_kind::counter
                ? ",\"sum\":{\"aggregationTemporality\":\"AGGREGATION_TEMPORALITY_CUMULATIVE\",\"isMonotonic\":true,\"dataPoints\":["
                : record.kind == otel_metric_kind::histogram
                ? ",\"histogram\":{\"aggregationTemporality\":\"AGGREGATION_TEMPORALITY_CUMULATIVE\",\"dataPoints\":["
                : ",\"gauge\":{\"dataPoints\":[";
            for (std::size_t point_index{}; point_index < record.points.size(); ++point_index)
            {
                if (point_index != 0U)
                    result.push_back(',');
                const auto& point = record.points[point_index];
                result += "{\"timeUnixNano\":";
                append_json_string(result, unix_nanoseconds(point.observed_at));
                if (record.kind != otel_metric_kind::gauge)
                {
                    result += ",\"startTimeUnixNano\":";
                    append_json_string(result, unix_nanoseconds(point.started_at));
                }
                if (record.kind == otel_metric_kind::histogram)
                    append_histogram(result, point, record.explicit_bounds);
                else
                    result += ",\"asDouble\":" + std::format("{:.{}g}", point.value, std::numeric_limits<double>::max_digits10);
                result += ",\"attributes\":[";
                if (point.overflow)
                    result += "{\"key\":\"otel.metric.overflow\",\"value\":{\"boolValue\":true}}";
                else
                    append_attributes(result, point.attributes);
                result += "]}";
            }
            result += "]}}";
        }
        result += "]}]}]}";
        return result;
    }

    auto signal_endpoint(std::string endpoint, std::string_view signal)
        -> std::string
    {
        constexpr std::string_view traces_suffix{"/v1/traces"};
        if (endpoint.ends_with(traces_suffix))
            endpoint.erase(endpoint.size() - traces_suffix.size());
        while (endpoint.ends_with('/'))
            endpoint.pop_back();
        return endpoint + "/v1/" + std::string{signal};
    }

} // namespace

class otlp_http_exporter_state : public std::enable_shared_from_this<otlp_http_exporter_state>
{
public:
    otlp_http_exporter_state(io_context& context, otlp_http_options value)
        : ctx(context), options(std::move(value)), client(context, http::client_options{.connect_timeout = options.request_timeout, .request_timeout = options.request_timeout, .follow_redirects = false, .keep_alive = true, .version_pref = http::http_version_preference::http1_only, .enable_cookies = false, .http1_response_body_limit = 64U * 1024U})
    {
        if (!options.endpoint.empty())
            queue.emplace(options.queue_capacity);
        if (!options.metrics_endpoint.empty())
        {
            metric_queue.emplace(options.queue_capacity);
            metric_aggregation.emplace(options.max_metric_instruments,
                options.max_metric_attribute_sets);
        }
        if (!options.logs_endpoint.empty())
            log_queue.emplace(options.queue_capacity);
    }

    io_context& ctx;
    otlp_http_options options;
    std::optional<concurrent_containers::bounded_mpmc_queue<http::tracing::completed_span>> queue;
    std::optional<concurrent_containers::bounded_mpmc_queue<otel_metric_record>> metric_queue;
    std::optional<instrumentation::metric_aggregation> metric_aggregation;
    std::optional<concurrent_containers::bounded_mpmc_queue<otel_log_record>> log_queue;
    http::client client;
    cancel_token delivery_token;
    bool aborted{};
    std::atomic<bool> accepting{true};
    std::atomic<bool> scheduled{};
    std::atomic<std::uint64_t> accepted{};
    std::atomic<std::uint64_t> dropped{};
    std::atomic<std::uint64_t> exported{};
    std::atomic<std::uint64_t> rejected_spans{};
    std::atomic<std::uint64_t> rejected_metric_points{};
    std::atomic<std::uint64_t> rejected_logs{};
    std::atomic<std::uint64_t> partial_batches{};
    std::atomic<std::uint64_t> warning_batches{};
    std::atomic<std::uint64_t> invalid_responses{};
    std::atomic<std::uint64_t> failed_batches{};
    std::atomic<std::uint64_t> retries{};
    std::atomic<std::uint64_t> accepted_spans{};
    std::atomic<std::uint64_t> accepted_metrics{};
    std::atomic<std::uint64_t> accepted_logs{};
    std::atomic<std::uint64_t> dropped_spans{};
    std::atomic<std::uint64_t> dropped_metrics{};
    std::atomic<std::uint64_t> dropped_logs{};
    std::atomic<std::uint64_t> worker_failures{};

    void worker_failed() noexcept
    {
        worker_failures.fetch_add(1U, std::memory_order_relaxed);
        scheduled.store(false, std::memory_order_release);
    }

    static auto retryable_status(int status) noexcept -> bool
    {
        return status == 429 || status == 502 || status == 503 || status == 504;
    }

    auto retry_delay(std::size_t attempt,
        const std::optional<http::response>& response) const -> std::chrono::milliseconds
    {
        return detail::export_retry_delay(options.initial_retry_delay,
            options.max_retry_delay, attempt,
            response ? response->get_header("Retry-After") : std::string_view{});
    }

    static void discard_post(void* value) noexcept
    {
        std::unique_ptr<std::shared_ptr<otlp_http_exporter_state>> state{
            static_cast<std::shared_ptr<otlp_http_exporter_state>*>(value)};
        (*state)->worker_failed();
    }

    static void start_drain(void* value) noexcept
    {
        std::unique_ptr<std::shared_ptr<otlp_http_exporter_state>> keep_alive{
            static_cast<std::shared_ptr<otlp_http_exporter_state>*>(value)};
        const auto state = *keep_alive;
        try
        {
            spawn_guarded(state->ctx, state->drain(), [state](std::exception_ptr) noexcept
                {
                    state->worker_failed();
                });
        }
        catch (...)
        {
            state->worker_failed();
        }
    }

    void schedule() noexcept
    {
        if (scheduled.exchange(true, std::memory_order_acq_rel))
            return;
        try
        {
            auto keep_alive = std::make_unique<std::shared_ptr<otlp_http_exporter_state>>(
                shared_from_this());
            ctx.post(&start_drain, keep_alive.get(), &discard_post);
            (void)keep_alive.release();
        }
        catch (...)
        {
            worker_failed();
        }
    }

    auto deliver(std::string_view endpoint, std::string body,
        std::string_view rejected_field, std::uint64_t sent)
        -> task<std::optional<detail::export_acknowledgement>>
    {
        http::request request{http::http_method::POST, std::string{endpoint}};
        request.set_header("Content-Type", "application/json");
        for (const auto& [key, value] : options.headers)
        {
            if (!std::ranges::equal(key, std::string_view{"content-type"},
                    [](char left, char right)
                    {
                        return std::tolower(static_cast<unsigned char>(left)) ==
                            std::tolower(static_cast<unsigned char>(right));
                    }))
                request.set_header(key, value);
        }
        request.set_header("Accept-Encoding", "identity");
        request.set_body(std::move(body));
        for (std::size_t attempt{}; attempt < options.max_attempts; ++attempt)
        {
            if (aborted)
                break;
            delivery_token.reset();
            auto result = co_await with_deadline(ctx, deadline::after(options.request_timeout),
                client.send(request, delivery_token), delivery_token);
            if (aborted)
                break;
            if (!result && (result.error() == http::make_error_code(http::http_errc::body_too_large) || result.error() == http::make_error_code(http::http_errc::header_too_large) || result.error() == http::make_error_code(http::http_errc::invalid_header) || result.error() == http::make_error_code(http::http_errc::invalid_chunk)))
            {
                invalid_responses.fetch_add(1U, std::memory_order_relaxed);
                co_return std::nullopt;
            }
            if (result && result->status_code() >= 200 &&
                result->status_code() < 300)
            {
                const auto encoding = result->get_header("Content-Encoding");
                if (!encoding.empty() && encoding != "identity")
                {
                    invalid_responses.fetch_add(1U, std::memory_order_relaxed);
                    co_return std::nullopt;
                }
                const auto acknowledgement = detail::parse_export_response(result->body(), rejected_field, sent);
                if (!acknowledgement)
                    invalid_responses.fetch_add(1U, std::memory_order_relaxed);
                co_return acknowledgement;
            }
            const auto can_retry = attempt + 1U < options.max_attempts &&
                (!result || retryable_status(result->status_code()));
            if (!can_retry)
                break;
            retries.fetch_add(1U, std::memory_order_relaxed);
            std::optional<http::response> response;
            if (result)
                response = std::move(*result);
            delivery_token.reset();
            const auto waited = co_await async_timer_wait(ctx,
                retry_delay(attempt, response), delivery_token);
            if (!waited)
                break;
        }
        co_return std::nullopt;
    }

    /**
     * @brief Accounts for delivery without retrying collector partial acceptance.
     */
    void account(const std::optional<detail::export_acknowledgement>& acknowledgement,
        std::uint64_t sent, std::atomic<std::uint64_t>& rejected) noexcept
    {
        if (!acknowledgement)
        {
            failed_batches.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        exported.fetch_add(sent - acknowledgement->rejected, std::memory_order_relaxed);
        rejected.fetch_add(acknowledgement->rejected, std::memory_order_relaxed);
        if (acknowledgement->partial)
            partial_batches.fetch_add(1U, std::memory_order_relaxed);
        if (acknowledgement->warning)
            warning_batches.fetch_add(1U, std::memory_order_relaxed);
    }

    /**
     * @brief Releases queued records and an idle connection without scheduling.
     * Call only on the execution thread after active delivery has settled.
     */
    void discard_pending() noexcept
    {
        while (queue && queue->try_dequeue())
        {
            dropped.fetch_add(1U, std::memory_order_relaxed);
            dropped_spans.fetch_add(1U, std::memory_order_relaxed);
        }
        while (log_queue && log_queue->try_dequeue())
        {
            dropped.fetch_add(1U, std::memory_order_relaxed);
            dropped_logs.fetch_add(1U, std::memory_order_relaxed);
        }
        while (metric_queue && metric_queue->try_dequeue())
        {
            dropped.fetch_add(1U, std::memory_order_relaxed);
            dropped_metrics.fetch_add(1U, std::memory_order_relaxed);
        }
        client.close();
    }

    auto drain() -> task<void>
    {
        for (;;)
        {
            if (aborted)
            {
                discard_pending();
                break;
            }
            bool found_work = false;
            std::vector<http::tracing::completed_span> batch;
            if (queue)
                batch.reserve(options.max_batch_size);
            while (batch.size() < options.max_batch_size)
            {
                auto span = queue ? queue->try_dequeue() : std::nullopt;
                if (!span)
                    break;
                batch.push_back(std::move(*span));
            }
            if (!batch.empty())
            {
                found_work = true;
                account(co_await deliver(options.endpoint, encode_batch(options, batch),
                            "rejectedSpans", batch.size()),
                    batch.size(), rejected_spans);
                if (aborted)
                    continue;
            }

            std::vector<otel_log_record> logs;
            if (log_queue)
                logs.reserve(options.max_batch_size);
            while (logs.size() < options.max_batch_size)
            {
                auto record = log_queue ? log_queue->try_dequeue() : std::nullopt;
                if (!record)
                    break;
                logs.push_back(std::move(*record));
            }
            if (!logs.empty())
            {
                found_work = true;
                account(co_await deliver(options.logs_endpoint, encode_logs(options, logs),
                            "rejectedLogRecords", logs.size()),
                    logs.size(), rejected_logs);
                if (aborted)
                    continue;
            }

            std::vector<otel_metric_record> metrics;
            if (metric_queue)
                metrics.reserve(options.max_batch_size);
            while (metrics.size() < options.max_batch_size)
            {
                auto metric = metric_queue ? metric_queue->try_dequeue() : std::nullopt;
                if (!metric)
                    break;
                metrics.push_back(std::move(*metric));
            }
            if (!metrics.empty())
            {
                found_work = true;
                std::size_t aggregated{};
                for (auto& measurement : metrics)
                {
                    if (metric_aggregation->record(std::move(measurement)))
                        ++aggregated;
                    else
                    {
                        dropped.fetch_add(1U, std::memory_order_relaxed);
                        dropped_metrics.fetch_add(1U, std::memory_order_relaxed);
                    }
                }
                if (aggregated != 0U)
                {
                    const auto snapshot = metric_aggregation->collect();
                    std::uint64_t points{};
                    for (const auto& series : snapshot)
                        points += series.points.size();
                    account(co_await deliver(options.metrics_endpoint, encode_metrics(options, snapshot),
                                "rejectedDataPoints", points),
                        points, rejected_metric_points);
                }
            }
            if (!found_work)
                break;
        }
        scheduled.store(false, std::memory_order_release);
        // `close()` stops producers, not the drain.  A span accepted before
        // close must not become stranded merely because the last drain pass
        // observed the queue during a producer/scheduler hand-off.
        if ((queue && queue->approximate_size() != 0U) ||
            (metric_queue && metric_queue->approximate_size() != 0U) ||
            (log_queue && log_queue->approximate_size() != 0U))
            schedule();
    }
};

otlp_http_exporter::otlp_http_exporter(io_context& context, otlp_http_options options)
{
    if (options.export_metrics && !options.endpoint.empty() && options.metrics_endpoint.empty())
        options.metrics_endpoint = signal_endpoint(options.endpoint, "metrics");
    if (options.export_logs && !options.endpoint.empty() && options.logs_endpoint.empty())
        options.logs_endpoint = signal_endpoint(options.endpoint, "logs");
    if (!options.export_traces)
        options.endpoint.clear();
    if (!options.export_metrics)
        options.metrics_endpoint.clear();
    if (!options.export_logs)
        options.logs_endpoint.clear();
    const auto enabled = !options.endpoint.empty() ||
        !options.metrics_endpoint.empty() || !options.logs_endpoint.empty();
    if (!enabled)
        return;
    options.queue_capacity = std::max<std::size_t>(2U, options.queue_capacity);
    options.max_batch_size = std::max<std::size_t>(1U, options.max_batch_size);
    options.max_attempts = std::max<std::size_t>(1U, options.max_attempts);
    options.initial_retry_delay = std::max(std::chrono::milliseconds::zero(),
        options.initial_retry_delay);
    options.max_retry_delay = std::max(options.initial_retry_delay,
        options.max_retry_delay);
    state_ = std::make_shared<otlp_http_exporter_state>(context, std::move(options));
    state_->accepting.store(enabled, std::memory_order_release);
}

otlp_http_exporter::~otlp_http_exporter()
{
    close();
}

auto otlp_http_exporter::submit(http::tracing::completed_span span) noexcept -> bool
{
    const auto state = state_;
    if (!state || state->options.endpoint.empty())
        return false;
    if (!state || !state->accepting.load(std::memory_order_acquire) ||
        !state->queue->try_enqueue(std::move(span)))
    {
        if (state)
        {
            state->dropped.fetch_add(1U, std::memory_order_relaxed);
            state->dropped_spans.fetch_add(1U, std::memory_order_relaxed);
        }
        return false;
    }
    state->accepted.fetch_add(1U, std::memory_order_relaxed);
    state->accepted_spans.fetch_add(1U, std::memory_order_relaxed);
    state->schedule();
    return true;
}

auto otlp_http_exporter::submit(otel_metric_record metric) noexcept -> bool
{
    const auto state = state_;
    if (!state || state->options.metrics_endpoint.empty())
        return false;
    if (!instrumentation::valid_metric_measurement(metric))
    {
        state->dropped.fetch_add(1U, std::memory_order_relaxed);
        state->dropped_metrics.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    if (!state || !state->accepting.load(std::memory_order_acquire) ||
        !state->metric_queue->try_enqueue(std::move(metric)))
    {
        if (state)
        {
            state->dropped.fetch_add(1U, std::memory_order_relaxed);
            state->dropped_metrics.fetch_add(1U, std::memory_order_relaxed);
        }
        return false;
    }
    state->accepted.fetch_add(1U, std::memory_order_relaxed);
    state->accepted_metrics.fetch_add(1U, std::memory_order_relaxed);
    state->schedule();
    return true;
}

auto otlp_http_exporter::submit(otel_log_record record) noexcept -> bool
{
    const auto state = state_;
    if (!state || state->options.logs_endpoint.empty())
        return false;
    if (!state || !state->accepting.load(std::memory_order_acquire) ||
        !state->log_queue->try_enqueue(std::move(record)))
    {
        if (state)
        {
            state->dropped.fetch_add(1U, std::memory_order_relaxed);
            state->dropped_logs.fetch_add(1U, std::memory_order_relaxed);
        }
        return false;
    }
    state->accepted.fetch_add(1U, std::memory_order_relaxed);
    state->accepted_logs.fetch_add(1U, std::memory_order_relaxed);
    state->schedule();
    return true;
}

auto otlp_http_exporter::flush(std::chrono::milliseconds timeout)
    -> task<std::expected<void, std::error_code>>
{
    const auto state = state_;
    if (!state)
        co_return {};

    timeout = std::max(timeout, std::chrono::milliseconds::zero());
    const auto expires_at = std::chrono::steady_clock::now() + timeout;
    while (state->scheduled.load(std::memory_order_acquire) ||
        (state->queue && state->queue->approximate_size() != 0U) ||
        (state->metric_queue && state->metric_queue->approximate_size() != 0U) ||
        (state->log_queue && state->log_queue->approximate_size() != 0U))
    {
        if (state->aborted && !state->scheduled.load(std::memory_order_acquire))
        {
            state->discard_pending();
            co_return {};
        }
        if (std::chrono::steady_clock::now() >= expires_at)
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        if (!state->scheduled.load(std::memory_order_acquire))
            state->schedule();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            expires_at - std::chrono::steady_clock::now());
        const auto waited = co_await async_timer_wait(state->ctx,
            std::max(std::chrono::milliseconds{1},
                std::min(std::chrono::milliseconds{5}, remaining)));
        if (!waited)
            co_return std::unexpected(waited.error());
    }
    co_return {};
}

auto otlp_http_exporter::statistics() const noexcept -> otlp_exporter_statistics
{
    const auto state = state_;
    if (!state)
        return {};
    return {
        .accepted = state->accepted.load(std::memory_order_relaxed),
        .dropped = state->dropped.load(std::memory_order_relaxed),
        .exported = state->exported.load(std::memory_order_relaxed),
        .failed_batches = state->failed_batches.load(std::memory_order_relaxed),
        .retries = state->retries.load(std::memory_order_relaxed),
        .accepted_spans = state->accepted_spans.load(std::memory_order_relaxed),
        .accepted_metrics = state->accepted_metrics.load(std::memory_order_relaxed),
        .accepted_logs = state->accepted_logs.load(std::memory_order_relaxed),
        .dropped_spans = state->dropped_spans.load(std::memory_order_relaxed),
        .dropped_metrics = state->dropped_metrics.load(std::memory_order_relaxed),
        .dropped_logs = state->dropped_logs.load(std::memory_order_relaxed),
        .worker_failures = state->worker_failures.load(std::memory_order_relaxed),
        .rejected_spans = state->rejected_spans.load(std::memory_order_relaxed),
        .rejected_metric_points = state->rejected_metric_points.load(std::memory_order_relaxed),
        .rejected_logs = state->rejected_logs.load(std::memory_order_relaxed),
        .partial_batches = state->partial_batches.load(std::memory_order_relaxed),
        .warning_batches = state->warning_batches.load(std::memory_order_relaxed),
        .invalid_responses = state->invalid_responses.load(std::memory_order_relaxed),
    };
}

void otlp_http_exporter::close() noexcept
{
    if (state_)
        state_->accepting.store(false, std::memory_order_release);
}

void otlp_http_exporter::abort() noexcept
{
    close();
    if (state_)
    {
        state_->aborted = true;
        state_->delivery_token.cancel();
        state_->client.close();
        if (!state_->scheduled.load(std::memory_order_acquire))
            state_->discard_pending();
    }
}

auto otlp_http_exporter::try_settle_shutdown() noexcept -> bool
{
    abort();
    return !state_ || !state_->scheduled.load(std::memory_order_acquire);
}

auto otlp_http_exporter::shutdown(std::chrono::milliseconds delivery_timeout,
    std::chrono::milliseconds cancellation_timeout)
    -> task<std::expected<void, std::error_code>>
{
    close();
    try
    {
        (void)co_await flush(delivery_timeout);
    }
    catch (...)
    {
        // Graceful delivery failure must never bypass cancellation.
    }
    if (try_settle_shutdown())
        co_return {};
    try
    {
        co_return co_await flush(cancellation_timeout);
    }
    catch (const std::bad_alloc&)
    {
        co_return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }
}

} // namespace cnetmod::observability
