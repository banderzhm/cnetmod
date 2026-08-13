module cnetmod.protocol.http.middleware.tracing;

import std;
import cnetmod.coro.task;

namespace cnetmod::http::tracing {
namespace {

    constexpr std::size_t trace_id_length = 32U;
    constexpr std::size_t span_id_length = 16U;
    constexpr std::size_t traceparent_length = 55U;
    constexpr std::size_t max_tracestate_length = 512U;

    [[nodiscard]] auto hex_value(char value) noexcept -> std::optional<std::uint8_t>
    {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f')
            return static_cast<std::uint8_t>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F')
            return static_cast<std::uint8_t>(value - 'A' + 10);
        return std::nullopt;
    }

    [[nodiscard]] auto is_hex(std::string_view value) noexcept -> bool
    {
        return std::ranges::all_of(value,
            [](char character)
            {
                return hex_value(character).has_value();
            });
    }

    [[nodiscard]] auto is_all_zero(std::string_view value) noexcept -> bool
    {
        return std::ranges::all_of(value,
            [](char character)
            {
                return character == '0';
            });
    }

    [[nodiscard]] auto has_valid_tracestate_characters(
        std::string_view value) noexcept -> bool
    {
        return std::ranges::all_of(value,
            [](unsigned char character)
            {
                // W3C tracestate is an HTTP field value. Restrict this
                // low-level propagation helper to visible ASCII plus SP so
                // an untrusted inbound value cannot inject another header.
                return character == ' ' || (character >= 0x21U && character <= 0x7eU);
            });
    }

    [[nodiscard]] auto lowercase_hex(std::string_view value) -> std::string
    {
        std::string normalized(value);
        std::ranges::transform(normalized, normalized.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return normalized;
    }

    [[nodiscard]] auto random_hex(std::size_t byte_count) -> std::string
    {
        static constexpr std::string_view hex{"0123456789abcdef"};
        static thread_local std::random_device random_device;
        std::string result;
        result.reserve(byte_count * 2U);
        for (std::size_t index{}; index < byte_count; ++index)
        {
            const auto value = static_cast<std::uint8_t>(random_device() & 0xffU);
            result.push_back(hex[(value >> 4U) & 0x0fU]);
            result.push_back(hex[value & 0x0fU]);
        }
        return result;
    }

    [[nodiscard]] auto context_is_valid(const trace_context& context) noexcept -> bool
    {
        return context.trace_id.size() == trace_id_length &&
            context.span_id.size() == span_id_length &&
            is_hex(context.trace_id) && is_hex(context.span_id) &&
            !is_all_zero(context.trace_id) && !is_all_zero(context.span_id);
    }

    void report_span(const tracing_options& options, const trace_context& context,
        std::string_view method, std::string_view path, int status,
        std::chrono::steady_clock::duration elapsed, bool has_remote_parent) noexcept
    {
        if (!options.on_end)
            return;
        try
        {
            options.on_end(completed_span{
                .context = context,
                .method = std::string(method),
                .path = std::string(path),
                .status_code = status,
                .elapsed = elapsed,
                .has_remote_parent = has_remote_parent,
            });
        }
        catch (...)
        {
            // Instrumentation must never fail the user request.
        }
    }

    auto run_traced_request(const tracing_options& options, request_context& request,
        next_fn next) -> task<void>
    {
        const auto incoming_state = options.accept_tracestate
            ? request.get_header("tracestate")
            : std::string_view{};
        auto parent = parse_traceparent(request.get_header("traceparent"), incoming_state);
        auto context = parent ? child_context(*parent) : new_root_context();
        request.set_trace_context(context.trace_id, context.span_id, context.flags,
            context.tracestate);
        if (options.emit_response_traceparent)
            request.resp().set_header("traceparent", format_traceparent(context));

        const auto started = std::chrono::steady_clock::now();
        try
        {
            co_await next();
        }
        catch (...)
        {
            report_span(options, context, request.method(), request.path(),
                request.resp().status_code(), std::chrono::steady_clock::now() - started,
                parent.has_value());
            throw;
        }
        report_span(options, context, request.method(), request.path(),
            request.resp().status_code(), std::chrono::steady_clock::now() - started,
            parent.has_value());
    }

} // namespace

auto parse_traceparent(std::string_view value, std::string_view tracestate)
    -> std::optional<trace_context>
{
    if (value.size() < traceparent_length || tracestate.size() > max_tracestate_length ||
        !has_valid_tracestate_characters(tracestate) ||
        value[2] != '-' || value[35] != '-' || value[52] != '-')
        return std::nullopt;

    const auto version = value.substr(0, 2);
    if (!is_hex(version) || lowercase_hex(version) == "ff")
        return std::nullopt;
    if (lowercase_hex(version) == "00" && value.size() != traceparent_length)
        return std::nullopt;
    if (value.size() > traceparent_length && value[traceparent_length] != '-')
        return std::nullopt;

    auto trace_id = value.substr(3, trace_id_length);
    auto span_id = value.substr(36, span_id_length);
    auto flags = value.substr(53, 2);
    if (!is_hex(trace_id) || !is_hex(span_id) || !is_hex(flags) ||
        is_all_zero(trace_id) || is_all_zero(span_id))
        return std::nullopt;

    const auto high = *hex_value(flags[0]);
    const auto low = *hex_value(flags[1]);
    return trace_context{
        .trace_id = lowercase_hex(trace_id),
        .span_id = lowercase_hex(span_id),
        .flags = static_cast<std::uint8_t>((high << 4U) | low),
        .tracestate = std::string(tracestate),
    };
}

auto new_root_context() -> trace_context
{
    return trace_context{
        .trace_id = random_hex(trace_id_length / 2U),
        .span_id = random_hex(span_id_length / 2U),
        .flags = 1U,
    };
}

auto child_context(const trace_context& parent) -> trace_context
{
    if (!context_is_valid(parent))
        return new_root_context();
    return trace_context{
        .trace_id = lowercase_hex(parent.trace_id),
        .span_id = random_hex(span_id_length / 2U),
        .flags = parent.flags,
        .tracestate = parent.tracestate,
    };
}

auto format_traceparent(const trace_context& context) -> std::string
{
    if (!context_is_valid(context))
        return {};
    static constexpr std::string_view hex{"0123456789abcdef"};
    std::string result{"00-"};
    result += lowercase_hex(context.trace_id);
    result += '-';
    result += lowercase_hex(context.span_id);
    result += '-';
    result.push_back(hex[(context.flags >> 4U) & 0x0fU]);
    result.push_back(hex[context.flags & 0x0fU]);
    return result;
}

void inject(request& destination, const trace_context& context)
{
    const auto header = format_traceparent(context);
    if (header.empty())
        return;
    destination.set_header("traceparent", header);
    if (context.tracestate.empty())
        destination.remove_header("tracestate");
    else
        destination.set_header("tracestate", context.tracestate);
}

auto context_from(const request_context& request) -> std::optional<trace_context>
{
    trace_context context{
        .trace_id = std::string(request.trace_id()),
        .span_id = std::string(request.trace_span_id()),
        .flags = request.trace_flags(),
        .tracestate = std::string(request.trace_state()),
    };
    return context_is_valid(context) ? std::optional{std::move(context)} : std::nullopt;
}

auto start_client_span(const trace_context& parent, std::string name,
    std::vector<std::pair<std::string, std::string>> attributes) -> active_span
{
    return {
        .context = child_context(parent),
        .name = std::move(name),
        .started = std::chrono::steady_clock::now(),
        .attributes = std::move(attributes),
    };
}

auto finish_client_span(active_span span, bool failed) -> completed_span
{
    return {
        .context = std::move(span.context),
        .name = std::move(span.name),
        .elapsed = std::chrono::steady_clock::now() - span.started,
        .failed = failed,
        .attributes = std::move(span.attributes),
    };
}

auto tracing_middleware(tracing_options options) -> middleware_fn
{
    return [options = std::move(options)](
               request_context& request, next_fn next) -> task<void>
    {
        co_await run_traced_request(options, request, std::move(next));
    };
}

} // namespace cnetmod::http::tracing
