module cnetmod.instrumentation.tracing;

import std;

namespace cnetmod::instrumentation {
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

auto start_client_span(const trace_context& parent, std::string name,
    std::vector<std::pair<std::string, std::string>> attributes) -> active_span
{
    return {
        .context = child_context(parent),
        .name = std::move(name),
        .started = std::chrono::steady_clock::now(),
        .attributes = std::move(attributes),
        .parent_span_id = context_is_valid(parent) ? parent.span_id : std::string{},
        .started_at = std::chrono::system_clock::now(),
        .kind = span_kind::client,
    };
}

auto start_server_span(const trace_context& parent, std::string name,
    std::vector<std::pair<std::string, std::string>> attributes) -> active_span
{
    return {
        .context = child_context(parent),
        .name = std::move(name),
        .started = std::chrono::steady_clock::now(),
        .attributes = std::move(attributes),
        .parent_span_id = context_is_valid(parent) ? parent.span_id : std::string{},
        .started_at = std::chrono::system_clock::now(),
        .kind = span_kind::server,
    };
}

auto finish_client_span(active_span span, bool failed) -> completed_span
{
    const auto ended_at = std::chrono::system_clock::now();
    return {
        .context = std::move(span.context),
        .name = std::move(span.name),
        .elapsed = std::chrono::steady_clock::now() - span.started,
        .failed = failed,
        .attributes = std::move(span.attributes),
        .parent_span_id = std::move(span.parent_span_id),
        .started_at = span.started_at,
        .ended_at = ended_at,
        .kind = span.kind,
        .result = {failed ? operation_status::error : operation_status::success, {}},
    };
}

span_exporter::span_exporter(sink_function sink, root_sampler sampler)
    : sink_(std::move(sink)), sampler_(std::move(sampler)) {}

span_exporter::operator bool() const noexcept
{
    return static_cast<bool>(sink_);
}

void span_exporter::operator()(const completed_span& span) const
{
    sink_(span);
}

auto span_exporter::sample(trace_context& context, bool has_parent) const noexcept -> bool
{
    bool sampled = (context.flags & 1U) != 0;
    if (!has_parent && sampler_)
    {
        try
        {
            sampled = sampler_(context);
        }
        catch (...)
        {
            sampled = false;
        }
    }
    context.flags = static_cast<std::uint8_t>((context.flags & 0xfeU) | (sampled ? 1U : 0U));
    return sampled;
}

auto valid_trace_context(const trace_context& context) noexcept -> bool
{
    return context_is_valid(context);
}

} // namespace cnetmod::instrumentation
