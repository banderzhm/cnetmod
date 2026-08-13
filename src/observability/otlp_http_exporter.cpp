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

    auto encode_batch(std::string_view service,
        const std::vector<http::tracing::completed_span>& spans) -> std::string
    {
        const auto ended = std::chrono::system_clock::now();
        std::string result{"{\"resourceSpans\":[{\"resource\":{\"attributes\":[{\"key\":\"service.name\",\"value\":{\"stringValue\":"};
        append_json_string(result, service);
        result += "}}]},\"scopeSpans\":[{\"scope\":{\"name\":\"cnetmod\"},\"spans\":[";
        for (std::size_t index{}; index < spans.size(); ++index)
        {
            if (index != 0U)
                result.push_back(',');
            const auto& span = spans[index];
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                span.elapsed);
            const auto started = ended - elapsed;
            result += "{\"traceId\":";
            append_json_string(result, span.context.trace_id);
            result += ",\"spanId\":";
            append_json_string(result, span.context.span_id);
            result += ",\"name\":";
            const auto name = span.name.empty() ? span.method + " " + span.path : span.name;
            append_json_string(result, name);
            result += ",\"kind\":\"";
            result += span.name.empty() ? "SPAN_KIND_SERVER" : "SPAN_KIND_CLIENT";
            result += "\",\"startTimeUnixNano\":";
            append_json_string(result, unix_nanoseconds(started));
            result += ",\"endTimeUnixNano\":";
            append_json_string(result, unix_nanoseconds(ended));
            result += ",\"attributes\":[";
            bool first_attribute = true;
            const auto append_attribute = [&](std::string_view key, std::string_view value)
            {
                if (!first_attribute)
                    result.push_back(',');
                first_attribute = false;
                result += "{\"key\":";
                append_json_string(result, key);
                result += ",\"value\":{\"stringValue\":";
                append_json_string(result, value);
                result += "}}";
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
            if (span.has_remote_parent)
                result += ",\"flags\":1";
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
            request.set_body(encode_batch(options.service_name, batch));
            const auto result = co_await client.send(request);
            if (result && result->status_code() >= 200 && result->status_code() < 300)
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
    };
}

void otlp_http_exporter::close() noexcept
{
    if (state_)
        state_->accepting.store(false, std::memory_order_release);
}

} // namespace cnetmod::observability
