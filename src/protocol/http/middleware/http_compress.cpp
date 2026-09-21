module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
    #include <zlib.h>
#endif

module cnetmod.protocol.http.middleware.compress;

import std;
import cnetmod.coro.semaphore;
import cnetmod.instrumentation.metric;
import cnetmod.protocol.http;

namespace cnetmod {
namespace {
    struct compression_state
    {
        explicit compression_state(std::size_t concurrency) noexcept
            : permits(std::max<std::size_t>(concurrency, 1U))
        {
        }

        async_semaphore permits;
    };

    struct permit_guard
    {
        async_semaphore* permits{};

        explicit permit_guard(async_semaphore& value) noexcept
            : permits(&value)
        {
        }

        ~permit_guard()
        {
            if (permits)
                permits->release();
        }

        permit_guard(const permit_guard&) = delete;
        auto operator=(const permit_guard&) -> permit_guard& = delete;
    };

    auto ascii_lower(char value) noexcept -> char
    {
        if (value >= 'A' && value <= 'Z')
            return static_cast<char>(value - 'A' + 'a');
        return value;
    }

    auto trim(std::string_view value) noexcept -> std::string_view
    {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);
        return value;
    }

    auto equal_ascii(std::string_view left, std::string_view right) noexcept
        -> bool
    {
        return left.size() == right.size() &&
            std::ranges::equal(left, right, [](char lhs, char rhs)
                {
                    return ascii_lower(lhs) == ascii_lower(rhs);
                });
    }

    auto parse_quality(std::string_view value) noexcept
        -> std::optional<std::uint16_t>
    {
        if (value.empty() || (value.front() != '0' && value.front() != '1'))
            return std::nullopt;

        const auto whole = value.front();
        value.remove_prefix(1);
        if (value.empty())
            return whole == '0' ? 0U : 1'000U;
        if (value.front() != '.')
            return std::nullopt;

        value.remove_prefix(1);
        if (value.size() > 3U)
            return std::nullopt;

        std::uint16_t fraction{};
        std::uint16_t scale = 100U;
        for (const auto digit : value)
        {
            if (digit < '0' || digit > '9' ||
                (whole == '1' && digit != '0'))
                return std::nullopt;
            fraction = static_cast<std::uint16_t>(
                fraction + static_cast<std::uint16_t>(digit - '0') * scale);
            scale = static_cast<std::uint16_t>(scale / 10U);
        }
        return whole == '0' ? fraction : 1'000U;
    }

    auto quality(std::string_view parameters) noexcept -> std::uint16_t
    {
        while (!parameters.empty())
        {
            const auto separator = parameters.find(';');
            auto parameter = trim(parameters.substr(0, separator));
            if (separator == std::string_view::npos)
                parameters = {};
            else
                parameters.remove_prefix(separator + 1);
            const auto equals = parameter.find('=');
            if (equals == std::string_view::npos ||
                !equal_ascii(trim(parameter.substr(0, equals)), "q"))
                continue;
            auto value = trim(parameter.substr(equals + 1));
            return parse_quality(value).value_or(0U);
        }
        return 1'000U;
    }

    auto accepts_gzip(std::string_view value) noexcept -> bool
    {
        std::optional<std::uint16_t> explicit_quality;
        std::optional<std::uint16_t> wildcard_quality;
        while (!value.empty())
        {
            const auto comma = value.find(',');
            auto item = trim(value.substr(0, comma));
            if (comma == std::string_view::npos)
                value = {};
            else
                value.remove_prefix(comma + 1);
            const auto semicolon = item.find(';');
            const auto coding = trim(item.substr(0, semicolon));
            const auto parameters = semicolon == std::string_view::npos
                ? std::string_view{}
                : item.substr(semicolon + 1);
            if (equal_ascii(coding, "gzip"))
                explicit_quality = quality(parameters);
            else if (coding == "*")
                wildcard_quality = quality(parameters);
        }
        if (explicit_quality)
            return *explicit_quality > 0U;
        return wildcard_quality && *wildcard_quality > 0U;
    }

    auto is_compressible(std::string_view content_type) -> bool
    {
        std::string lowered;
        lowered.reserve(content_type.size());
        for (const auto character : content_type)
            lowered.push_back(ascii_lower(character));
        return lowered.starts_with("text/") ||
            lowered.find("json") != std::string::npos ||
            lowered.find("xml") != std::string::npos ||
            lowered.find("javascript") != std::string::npos ||
            lowered.find("svg") != std::string::npos;
    }

    auto contains_header_token(std::string_view value, std::string_view token)
        -> bool
    {
        while (!value.empty())
        {
            const auto comma = value.find(',');
            if (equal_ascii(trim(value.substr(0, comma)), token))
                return true;
            if (comma == std::string_view::npos)
                break;
            value.remove_prefix(comma + 1);
        }
        return false;
    }

    void merge_vary(http::response& response, std::string_view token)
    {
        const auto existing = response.get_header("Vary");
        if (contains_header_token(existing, token))
            return;
        if (existing.empty())
            response.set_header("Vary", token);
        else
            response.set_header("Vary",
                std::string{existing} + ", " + std::string{token});
    }

    void measure(const instrumentation::metric_sink& sink,
        std::string name, double value, instrumentation::metric_kind kind,
        std::vector<std::pair<std::string, std::string>> attributes = {}) noexcept
    {
        if (!sink)
            return;
        try
        {
            sink({.name = std::move(name),
                .value = value,
                .kind = kind,
                .attributes = std::move(attributes)});
        }
        catch (...)
        {
        }
    }

    void count_outcome(const compress_options& options,
        std::string outcome) noexcept
    {
        measure(options.measurements,
            "http.server.response.compression.operations", 1.0,
            instrumentation::metric_kind::counter,
            {{"content.encoding", "gzip"}, {"outcome", std::move(outcome)}});
    }

#ifdef CNETMOD_HAS_ZLIB
    auto gzip_compress(std::string input, int level)
        -> std::expected<std::string, std::error_code>
    {
        if (input.size() > static_cast<std::size_t>(
                               std::numeric_limits<uInt>::max()))
            return std::unexpected(
                std::make_error_code(std::errc::value_too_large));
        const auto bound = ::compressBound(static_cast<uLong>(input.size()));
        std::vector<Bytef> output(bound + 32U);
        z_stream stream{};
        if (deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8,
                Z_DEFAULT_STRATEGY) != Z_OK)
            return std::unexpected(std::make_error_code(std::errc::io_error));
        stream.next_in = reinterpret_cast<Bytef*>(input.data());
        stream.avail_in = static_cast<uInt>(input.size());
        stream.next_out = output.data();
        stream.avail_out = static_cast<uInt>(output.size());
        const auto result = deflate(&stream, Z_FINISH);
        const auto size = output.size() - stream.avail_out;
        deflateEnd(&stream);
        if (result != Z_STREAM_END)
            return std::unexpected(std::make_error_code(std::errc::io_error));
        return std::string(
            reinterpret_cast<const char*>(output.data()), size);
    }
#endif

    auto compress_response(compress_options options,
        std::shared_ptr<compression_state> state,
        http::request_context& context, http::next_fn next) -> task<void>
    {
        co_await next();
#ifdef CNETMOD_HAS_ZLIB
        if (!accepts_gzip(context.get_header("Accept-Encoding")) ||
            !context.resp().get_header("Content-Encoding").empty() ||
            context.resp().get_header("X-Streamed") == "1")
            co_return;
        const auto content_type = context.resp().get_header("Content-Type");
        if (!is_compressible(content_type))
            co_return;
        const auto source_size = context.resp().body().size();
        if (source_size < options.min_size)
            co_return;
        if (context.cancellation_token().is_cancelled())
        {
            count_outcome(options, "cancelled");
            co_return;
        }
        if (!state->permits.try_acquire())
        {
            count_outcome(options, "capacity");
            co_return;
        }
        permit_guard permit{state->permits};
        auto operation = [body = std::string{context.resp().body()},
                             level = options.level]() mutable
            -> std::expected<std::string, std::error_code>
        {
            return gzip_compress(std::move(body), level);
        };
        const auto started = std::chrono::steady_clock::now();
        std::expected<std::string, std::error_code> compressed;
        try
        {
            if (options.dispatch)
                compressed = co_await options.dispatch(std::move(operation),
                    context.cancellation_token());
            else
                compressed = operation();
        }
        catch (const std::system_error& error)
        {
            compressed = std::unexpected(error.code());
        }
        catch (const std::bad_alloc&)
        {
            compressed = std::unexpected(
                std::make_error_code(std::errc::not_enough_memory));
        }
        catch (...)
        {
            compressed = std::unexpected(
                std::make_error_code(std::errc::io_error));
        }
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                 .count();
        measure(options.measurements,
            "http.server.response.compression.duration", elapsed,
            instrumentation::metric_kind::histogram,
            {{"content.encoding", "gzip"}});
        if (context.cancellation_token().is_cancelled())
        {
            count_outcome(options, "cancelled");
            co_return;
        }
        if (!compressed)
        {
            count_outcome(options, "error");
            co_return;
        }
        if (compressed->size() >= source_size)
        {
            count_outcome(options, "larger");
            co_return;
        }
        measure(options.measurements,
            "http.server.response.compression.input.size",
            static_cast<double>(source_size),
            instrumentation::metric_kind::histogram,
            {{"content.encoding", "gzip"}});
        measure(options.measurements,
            "http.server.response.compression.output.size",
            static_cast<double>(compressed->size()),
            instrumentation::metric_kind::histogram,
            {{"content.encoding", "gzip"}});
        context.resp().set_body(std::move(*compressed));
        context.resp().set_header("Content-Encoding", "gzip");
        context.resp().set_header("Content-Length",
            std::to_string(context.resp().body().size()));
        merge_vary(context.resp(), "Accept-Encoding");
        count_outcome(options, "compressed");
#else
        (void)options;
        (void)state;
        (void)context;
#endif
    }
} // namespace

auto compress(compress_options options) -> http::middleware_fn
{
    auto state = std::make_shared<compression_state>(
        options.max_concurrency);
    return [options = std::move(options), state = std::move(state)](
               http::request_context& context, http::next_fn next)
               -> task<void>
    {
        return compress_response(options, state, context, std::move(next));
    };
}
} // namespace cnetmod
