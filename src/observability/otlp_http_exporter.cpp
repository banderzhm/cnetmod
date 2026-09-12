module cnetmod.observability.otlp;

import std;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.http;
import cnetmod.utils.concurrent_containers.queue;

namespace cnetmod::observability {
namespace {

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
                if (!first_attribute)
                    result.push_back(',');
                first_attribute = false;
                result += "{\"key\":\"http.response.status_code\",\"value\":{\"intValue\":" +
                    std::to_string(span.status_code) + "}}";
            }
            for (const auto& [key, value] : span.attributes)
                append_attribute(key, value);
            result += "]";
            if (span.failed)
                result += ",\"status\":{\"code\":\"STATUS_CODE_ERROR\"}";
            result += ",\"flags\":" + std::to_string(span.context.flags);
            result.push_back('}');
        }
        result += "]}]}]}";
        return result;
    }

} // namespace

class otlp_http_exporter_state : public std::enable_shared_from_this<otlp_http_exporter_state>
{
public:
    otlp_http_exporter_state(io_context& context, otlp_http_options value)
        : ctx(context), options(std::move(value)), queue(options.queue_capacity), client(context, http::client_options{.connect_timeout = options.request_timeout, .request_timeout = options.request_timeout, .follow_redirects = false, .keep_alive = true}) {}

    io_context& ctx;
    otlp_http_options options;
    concurrent_containers::bounded_mpmc_queue<http::tracing::completed_span> queue;
    http::client client;
    std::atomic<bool> accepting{true};
    std::atomic<bool> scheduled{};
    std::atomic<std::uint64_t> accepted{};
    std::atomic<std::uint64_t> dropped{};
    std::atomic<std::uint64_t> exported{};
    std::atomic<std::uint64_t> failed_batches{};
    std::atomic<std::uint64_t> retries{};

    static auto retryable_status(int status) noexcept -> bool
    {
        return status == 429 || status == 502 || status == 503 || status == 504;
    }

    auto retry_delay(std::size_t attempt,
        const std::optional<http::response>& response) const -> std::chrono::milliseconds
    {
        if (response)
        {
            const auto value = response->get_header("Retry-After");
            unsigned long long seconds{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                seconds);
            if (!value.empty() && parsed.ec == std::errc{} &&
                parsed.ptr == value.data() + value.size())
                return std::min(options.max_retry_delay,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::seconds{seconds}));
        }
        const auto shift = std::min<std::size_t>(attempt, 16U);
        const auto multiplier = static_cast<std::int64_t>(
            std::uint64_t{1} << shift);
        const auto raw = options.initial_retry_delay * multiplier;
        return std::min(options.max_retry_delay, raw);
    }

    static void discard_post(void* value) noexcept
    {
        delete static_cast<std::shared_ptr<otlp_http_exporter_state>*>(value);
    }

    static void start_drain(void* value) noexcept
    {
        std::unique_ptr<std::shared_ptr<otlp_http_exporter_state>> keep_alive{
            static_cast<std::shared_ptr<otlp_http_exporter_state>*>(value)};
        spawn((*keep_alive)->ctx, (*keep_alive)->drain());
    }

    void schedule() noexcept
    {
        if (scheduled.exchange(true, std::memory_order_acq_rel))
            return;
        auto* keep_alive = new std::shared_ptr<otlp_http_exporter_state>(
            shared_from_this());
        ctx.post(&start_drain, keep_alive, &discard_post);
    }

    auto drain() -> task<void>
    {
        for (;;)
        {
            std::vector<http::tracing::completed_span> batch;
            batch.reserve(options.max_batch_size);
            while (batch.size() < options.max_batch_size)
            {
                auto span = queue.try_dequeue();
                if (!span)
                    break;
                batch.push_back(std::move(*span));
            }
            if (batch.empty())
                break;

            http::request request{http::http_method::POST, options.endpoint};
            request.set_header("Content-Type", "application/json");
            for (const auto& [key, value] : options.headers)
                if (!std::ranges::equal(key, std::string_view{"content-type"},
                        [](char left, char right)
                        {
                            return std::tolower(static_cast<unsigned char>(left)) ==
                                std::tolower(static_cast<unsigned char>(right));
                        }))
                    request.set_header(key, value);
            request.set_body(encode_batch(options, batch));

            bool delivered = false;
            for (std::size_t attempt{}; attempt < options.max_attempts; ++attempt)
            {
                auto result = co_await client.send(request);
                if (result && result->status_code() >= 200 &&
                    result->status_code() < 300)
                {
                    delivered = true;
                    break;
                }
                const auto can_retry = attempt + 1U < options.max_attempts &&
                    (!result || retryable_status(result->status_code()));
                if (!can_retry)
                    break;
                retries.fetch_add(1U, std::memory_order_relaxed);
                std::optional<http::response> response;
                if (result)
                    response = std::move(*result);
                const auto waited = co_await async_timer_wait(ctx,
                    retry_delay(attempt, response));
                if (!waited)
                    break;
            }
            if (delivered)
                exported.fetch_add(batch.size(), std::memory_order_relaxed);
            else
                failed_batches.fetch_add(1U, std::memory_order_relaxed);
        }
        scheduled.store(false, std::memory_order_release);
        // `close()` stops producers, not the drain.  A span accepted before
        // close must not become stranded merely because the last drain pass
        // observed the queue during a producer/scheduler hand-off.
        if (queue.approximate_size() != 0U)
            schedule();
    }
};

otlp_http_exporter::otlp_http_exporter(io_context& context, otlp_http_options options)
{
    if (options.endpoint.empty())
        options.endpoint = "http://127.0.0.1:4318/v1/traces";
    options.queue_capacity = std::max<std::size_t>(2U, options.queue_capacity);
    options.max_batch_size = std::max<std::size_t>(1U, options.max_batch_size);
    options.max_attempts = std::max<std::size_t>(1U, options.max_attempts);
    options.initial_retry_delay = std::max(std::chrono::milliseconds::zero(),
        options.initial_retry_delay);
    options.max_retry_delay = std::max(options.initial_retry_delay,
        options.max_retry_delay);
    state_ = std::make_shared<otlp_http_exporter_state>(context, std::move(options));
}

otlp_http_exporter::~otlp_http_exporter()
{
    close();
}

auto otlp_http_exporter::submit(http::tracing::completed_span span) noexcept -> bool
{
    const auto state = state_;
    if (!state || !state->accepting.load(std::memory_order_acquire) ||
        !state->queue.try_enqueue(std::move(span)))
    {
        if (state)
            state->dropped.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    state->accepted.fetch_add(1U, std::memory_order_relaxed);
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
    state->schedule();
    const auto expires_at = std::chrono::steady_clock::now() + timeout;
    while (state->scheduled.load(std::memory_order_acquire) ||
        state->queue.approximate_size() != 0U)
    {
        if (std::chrono::steady_clock::now() >= expires_at)
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
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
    };
}

void otlp_http_exporter::close() noexcept
{
    if (state_)
        state_->accepting.store(false, std::memory_order_release);
}

} // namespace cnetmod::observability
