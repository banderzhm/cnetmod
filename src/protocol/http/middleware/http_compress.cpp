module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
    #include <zlib.h>
#endif
#ifdef CNETMOD_HAS_BROTLI
    #include <brotli/encode.h>
#endif

module cnetmod.protocol.http.middleware.compress;

import std;

import cnetmod.protocol.http;
import cnetmod.coro.semaphore;
import cnetmod.instrumentation.metric;
import cnetmod.protocol.http.semantics;
import cnetmod.utils.concurrent_containers.striped_hash_map;

namespace cnetmod {
namespace {
    struct cached_compression
    {
        std::string source;
        std::shared_ptr<const std::string> encoded;
    };

    struct compression_state
    {
        compression_state(std::size_t concurrency, std::size_t cache_capacity)
            : permits(std::max<std::size_t>(concurrency, 1U)),
              available_cache_slots(cache_capacity)
        {
            if (cache_capacity != 0U)
            {
                cache = std::make_unique<cache_type>(
                    std::max<std::size_t>(cache_capacity, 4U), 16U);
            }
        }

        using cache_type = concurrent_containers::striped_hash_map<std::string,
            std::shared_ptr<const cached_compression>>;

        async_semaphore permits;
        std::unique_ptr<cache_type> cache;
        std::atomic<std::size_t> available_cache_slots;
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

    struct coding_preferences
    {
        std::optional<std::uint16_t> gzip_quality;
        std::optional<std::uint16_t> brotli_quality;
        std::optional<std::uint16_t> wildcard_quality;
    };

    auto parse_coding_preferences(std::string_view value) noexcept
        -> coding_preferences
    {
        coding_preferences result;
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
                result.gzip_quality = quality(parameters);
            else if (equal_ascii(coding, "br"))
                result.brotli_quality = quality(parameters);
            else if (coding == "*")
                result.wildcard_quality = quality(parameters);
        }
        return result;
    }

    auto selected_quality(const std::optional<std::uint16_t>& explicit_quality,
        const std::optional<std::uint16_t>& wildcard_quality) noexcept
        -> std::uint16_t
    {
        return explicit_quality.value_or(wildcard_quality.value_or(0U));
    }

    enum class content_coding : std::uint8_t
    {
        none,
        gzip,
        brotli
    };

    auto negotiate(std::string_view accept_encoding, const compress_options& options) noexcept
        -> content_coding
    {
        const auto preferences = parse_coding_preferences(accept_encoding);
        const auto gzip = selected_quality(
            preferences.gzip_quality, preferences.wildcard_quality);
#ifdef CNETMOD_HAS_BROTLI
        if (options.brotli_quality >= 0)
        {
            const auto brotli = selected_quality(
                preferences.brotli_quality, preferences.wildcard_quality);
            if (brotli > 0U && brotli >= gzip)
                return content_coding::brotli;
        }
#else
        (void)options;
#endif
        return gzip > 0U ? content_coding::gzip : content_coding::none;
    }

    auto coding_name(content_coding coding) noexcept -> const char*
    {
        return coding == content_coding::brotli ? "br" : "gzip";
    }

    auto cache_key_for(const compress_options& options,
        const http::request_context& context, content_coding coding)
        -> std::optional<std::string>
    {
        if (options.cache_capacity == 0U || !options.cache_key)
            return std::nullopt;
        std::optional<std::string> key;
        try
        {
            key = options.cache_key(context);
        }
        catch (...)
        {
            return std::nullopt;
        }
        if (!key)
            return std::nullopt;
        key->push_back('\0');
        key->push_back(static_cast<char>(coding));
        return key;
    }

    auto reserve_cache_slot(compression_state& state) noexcept -> bool
    {
        auto available = state.available_cache_slots.load(
            std::memory_order_relaxed);
        while (available != 0U)
        {
            if (state.available_cache_slots.compare_exchange_weak(available,
                    available - 1U, std::memory_order_acq_rel,
                    std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    void store_cached(compression_state& state, const std::string& key,
        std::string_view source, std::shared_ptr<const std::string> encoded,
        bool replacing)
    {
        if (!state.cache)
            return;
        auto value = std::make_shared<const cached_compression>(
            cached_compression{.source = std::string{source},
                .encoded = std::move(encoded)});
        if (replacing)
        {
            state.cache->insert_or_assign(key, std::move(value));
            return;
        }
        if (!reserve_cache_slot(state))
            return;
        if (!state.cache->try_emplace(key, value))
        {
            // Another request inserted the same cold key after our lookup.
            // Return the reservation so concurrent misses cannot consume the
            // entire bounded capacity with duplicate keys, then refresh it.
            state.available_cache_slots.fetch_add(
                1U, std::memory_order_release);
            state.cache->insert_or_assign(key, std::move(value));
        }
    }

    auto starts_with_ascii(std::string_view value, std::string_view prefix) noexcept
        -> bool
    {
        return value.size() >= prefix.size() &&
            std::ranges::equal(value.substr(0, prefix.size()), prefix,
                [](char lhs, char rhs)
                {
                    return ascii_lower(lhs) == ascii_lower(rhs);
                });
    }

    auto contains_ascii(std::string_view value, std::string_view needle) noexcept
        -> bool
    {
        if (needle.empty())
            return true;
        if (needle.size() > value.size())
            return false;
        for (std::size_t offset = 0; offset <= value.size() - needle.size(); ++offset)
            if (starts_with_ascii(value.substr(offset), needle))
                return true;
        return false;
    }

    auto is_compressible(std::string_view content_type) noexcept -> bool
    {
        return starts_with_ascii(content_type, "text/") ||
            contains_ascii(content_type, "json") ||
            contains_ascii(content_type, "xml") ||
            contains_ascii(content_type, "javascript") ||
            contains_ascii(content_type, "svg");
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

    using metric_attribute_view = std::pair<std::string_view, std::string_view>;

    void measure(const instrumentation::metric_sink& sink,
        std::string_view name, double value, instrumentation::metric_kind kind,
        std::initializer_list<metric_attribute_view> attribute_views = {}) noexcept
    {
        if (!sink)
            return;
        try
        {
            std::vector<std::pair<std::string, std::string>> attributes;
            attributes.reserve(attribute_views.size());
            for (const auto& [key, attribute_value] : attribute_views)
                attributes.emplace_back(key, attribute_value);
            sink({.name = std::string{name},
                .value = value,
                .kind = kind,
                .attributes = std::move(attributes)});
        }
        catch (...)
        {
        }
    }

    void count_outcome(const compress_options& options, content_coding coding,
        std::string_view outcome) noexcept
    {
        measure(options.measurements,
            "http.server.response.compression.operations", 1.0,
            instrumentation::metric_kind::counter,
            {{"content.encoding", coding_name(coding)}, {"outcome", std::move(outcome)}});
    }

#ifdef CNETMOD_HAS_ZLIB
    class gzip_encoder
    {
    public:
        gzip_encoder() = default;

        ~gzip_encoder()
        {
            if (initialized_)
                ::deflateEnd(&stream_);
        }

        gzip_encoder(const gzip_encoder&) = delete;
        auto operator=(const gzip_encoder&) -> gzip_encoder& = delete;

        auto compress(std::string_view input, int level, std::string& output)
            -> std::expected<void, std::error_code>
        {
            if (input.size() > static_cast<std::size_t>(
                                   std::numeric_limits<uInt>::max()))
                return std::unexpected(
                    std::make_error_code(std::errc::value_too_large));

            if (!prepare(level))
                return std::unexpected(
                    std::make_error_code(std::errc::io_error));

            const auto bound = ::deflateBound(&stream_,
                static_cast<uLong>(input.size()));
            if (bound > static_cast<uLong>(
                            std::numeric_limits<uInt>::max()))
                return std::unexpected(
                    std::make_error_code(std::errc::value_too_large));

            // The string is the final response body. Writing into it directly
            // avoids the former temporary vector followed by a second copy.
            output.resize(static_cast<std::size_t>(bound));
            stream_.next_in = reinterpret_cast<Bytef*>(
                const_cast<char*>(input.data()));
            stream_.avail_in = static_cast<uInt>(input.size());
            stream_.next_out = reinterpret_cast<Bytef*>(output.data());
            stream_.avail_out = static_cast<uInt>(output.size());
            const auto result = ::deflate(&stream_, Z_FINISH);
            if (result != Z_STREAM_END)
                return std::unexpected(
                    std::make_error_code(std::errc::io_error));
            output.resize(static_cast<std::size_t>(stream_.total_out));
            return {};
        }

    private:
        auto prepare(int level) noexcept -> bool
        {
            if (initialized_ && level == level_)
                return ::deflateReset(&stream_) == Z_OK;
            if (initialized_)
            {
                ::deflateEnd(&stream_);
                stream_ = {};
                initialized_ = false;
            }
            if (::deflateInit2(&stream_, level, Z_DEFLATED, 15 + 16, 8,
                    Z_DEFAULT_STRATEGY) != Z_OK)
                return false;
            initialized_ = true;
            level_ = level;
            return true;
        }

        z_stream stream_{};
        int level_{};
        bool initialized_{};
    };

    auto gzip_compress_into(std::string_view input, int level,
        std::string& output) -> std::expected<void, std::error_code>
    {
        // Compression is synchronous inside the selected executor. One codec
        // workspace per executor thread is therefore reusable without locks.
        thread_local gzip_encoder encoder;
        return encoder.compress(input, level, output);
    }

    auto gzip_compress(std::string_view input, int level)
        -> std::expected<std::string, std::error_code>
    {
        std::string output;
        auto compressed = gzip_compress_into(input, level, output);
        if (!compressed)
            return std::unexpected(compressed.error());
        return output;
    }
#endif
#ifdef CNETMOD_HAS_BROTLI
    auto brotli_compress_into(std::string_view input, int quality, int window,
        std::string& output) -> std::expected<void, std::error_code>
    {
        const auto maximum = ::BrotliEncoderMaxCompressedSize(input.size());
        if (maximum == 0U)
            return std::unexpected(std::make_error_code(std::errc::value_too_large));
        output.resize(maximum);
        auto size = maximum;
        if (::BrotliEncoderCompress(std::clamp(quality, BROTLI_MIN_QUALITY, BROTLI_MAX_QUALITY),
                std::clamp(window, BROTLI_MIN_WINDOW_BITS, BROTLI_MAX_WINDOW_BITS),
                BROTLI_MODE_TEXT, input.size(),
                reinterpret_cast<const std::uint8_t*>(input.data()), &size,
                reinterpret_cast<std::uint8_t*>(output.data())) != BROTLI_TRUE)
            return std::unexpected(std::make_error_code(std::errc::io_error));
        output.resize(size);
        return {};
    }

    auto brotli_compress(std::string_view input, int quality, int window)
        -> std::expected<std::string, std::error_code>
    {
        std::string output;
        auto compressed = brotli_compress_into(
            input, quality, window, output);
        if (!compressed)
            return std::unexpected(compressed.error());
        return output;
    }
#endif

    auto compress_response(compress_options options,
        std::shared_ptr<compression_state> state,
        http::request_context& context, http::next_fn next) -> task<void>
    {
        co_await next();
#ifdef CNETMOD_HAS_ZLIB
        const auto coding = negotiate(context.get_header("Accept-Encoding"), options);
        if (coding == content_coding::none ||
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
            count_outcome(options, coding, "cancelled");
            co_return;
        }
        // The response body remains stable while an optional dispatcher owns
        // the operation. The inline path instead writes into response-owned
        // scratch storage so persistent connections retain both capacities.
        const auto body = context.resp().body();
        auto cache_key = cache_key_for(options, context, coding);
        bool replacing_cached = false;
        if (cache_key)
        {
            if (auto cached = state->cache->find(*cache_key))
            {
                replacing_cached = true;
                if ((*cached)->source == body)
                {
                    context.resp().set_shared_body((*cached)->encoded);
                    context.resp().set_header(
                        "Content-Encoding", coding_name(coding));
                    merge_vary(context.resp(), "Accept-Encoding");
                    count_outcome(options, coding, "cached");
                    co_return;
                }
            }
        }
        if (!state->permits.try_acquire())
        {
            count_outcome(options, coding, "capacity");
            co_return;
        }
        permit_guard permit{state->permits};
        const auto started = std::chrono::steady_clock::now();
        if (!options.dispatch)
        {
            std::expected<bool, std::error_code> transformed;
            try
            {
                transformed = context.resp().transform_body(
                    [level = options.level, coding,
                        quality = options.brotli_quality,
                        window = options.brotli_window](
                        std::string_view input, std::string& output)
                        -> std::expected<bool, std::error_code>
                    {
                        std::expected<void, std::error_code> compressed;
    #ifdef CNETMOD_HAS_BROTLI
                        if (coding == content_coding::brotli)
                            compressed = brotli_compress_into(
                                input, quality, window, output);
                        else
    #else
                        (void)quality;
                        (void)window;
    #endif
                            compressed = gzip_compress_into(input, level, output);
                        if (!compressed)
                            return std::unexpected(compressed.error());
                        return output.size() < input.size();
                    });
            }
            catch (const std::bad_alloc&)
            {
                transformed = std::unexpected(
                    std::make_error_code(std::errc::not_enough_memory));
            }
            catch (...)
            {
                transformed = std::unexpected(
                    std::make_error_code(std::errc::io_error));
            }
            const auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started)
                                     .count();
            measure(options.measurements,
                "http.server.response.compression.duration", elapsed,
                instrumentation::metric_kind::histogram,
                {{"content.encoding", coding_name(coding)}});
            if (!transformed)
            {
                count_outcome(options, coding, "error");
                co_return;
            }
            if (!*transformed)
            {
                count_outcome(options, coding, "larger");
                co_return;
            }
            measure(options.measurements,
                "http.server.response.compression.input.size",
                static_cast<double>(source_size),
                instrumentation::metric_kind::histogram,
                {{"content.encoding", coding_name(coding)}});
            measure(options.measurements,
                "http.server.response.compression.output.size",
                static_cast<double>(context.resp().body().size()),
                instrumentation::metric_kind::histogram,
                {{"content.encoding", coding_name(coding)}});
            context.resp().set_header("Content-Encoding", coding_name(coding));
            merge_vary(context.resp(), "Accept-Encoding");
            if (cache_key)
            {
                auto encoded = std::make_shared<const std::string>(
                    context.resp().body());
                store_cached(*state, *cache_key, body, encoded,
                    replacing_cached);
                context.resp().set_shared_body(std::move(encoded));
            }
            count_outcome(options, coding, "compressed");
            co_return;
        }
        auto operation = [body,
                             level = options.level, coding,
                             quality = options.brotli_quality,
                             window = options.brotli_window]() mutable
            -> std::expected<std::string, std::error_code>
        {
    #ifdef CNETMOD_HAS_BROTLI
            if (coding == content_coding::brotli)
                return brotli_compress(body, quality, window);
    #else
            (void)quality;
            (void)window;
    #endif
            return gzip_compress(body, level);
        };
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
            {{"content.encoding", coding_name(coding)}});
        if (context.cancellation_token().is_cancelled())
        {
            count_outcome(options, coding, "cancelled");
            co_return;
        }
        if (!compressed)
        {
            count_outcome(options, coding, "error");
            co_return;
        }
        if (compressed->size() >= source_size)
        {
            count_outcome(options, coding, "larger");
            co_return;
        }
        measure(options.measurements,
            "http.server.response.compression.input.size",
            static_cast<double>(source_size),
            instrumentation::metric_kind::histogram,
            {{"content.encoding", coding_name(coding)}});
        measure(options.measurements,
            "http.server.response.compression.output.size",
            static_cast<double>(compressed->size()),
            instrumentation::metric_kind::histogram,
            {{"content.encoding", coding_name(coding)}});
        if (cache_key)
        {
            auto encoded = std::make_shared<const std::string>(
                std::move(*compressed));
            store_cached(*state, *cache_key, body, encoded,
                replacing_cached);
            context.resp().set_shared_body(std::move(encoded));
        }
        else
            context.resp().set_body(std::move(*compressed));
        context.resp().set_header("Content-Encoding", coding_name(coding));
        context.resp().set_header("Content-Length",
            std::to_string(context.resp().body().size()));
        merge_vary(context.resp(), "Accept-Encoding");
        count_outcome(options, coding, "compressed");
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
        options.max_concurrency, options.cache_capacity);
    return [options = std::move(options), state = std::move(state)](
               http::request_context& context, http::next_fn next)
               -> task<void>
    {
        return compress_response(options, state, context, std::move(next));
    };
}
} // namespace cnetmod
