#include "test_framework.hpp"
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
    #include <zlib.h>
#endif
#ifdef CNETMOD_HAS_BROTLI
    #include <brotli/decode.h>
#endif

import std;
import cnetmod.core.socket;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.instrumentation.metric;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.compress;

namespace {
struct compression_observation
{
    bool dispatched{};
    std::vector<cnetmod::instrumentation::metric_measurement> measurements;
    cnetmod::http::response response;
};

auto run_compression(std::string accept_encoding,
    std::string vary = {}, bool cancel = false, int brotli_quality = -1,
    std::string payload = std::string(4096, 'a'), bool use_dispatch = true)
    -> compression_observation
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::header_map headers;
    headers["Accept-Encoding"] = std::move(accept_encoding);
    cnetmod::http::response response;
    if (!vary.empty())
        response.set_header("Vary", std::move(vary));
    cnetmod::http::request_context request{
        *io, socket, "GET", "/", headers, {}, response, {}};
    compression_observation observation;
    cnetmod::compress_options options{
        .min_size = 1,
        .brotli_quality = brotli_quality,
        .max_concurrency = 1,
        .dispatch = [&observation](cnetmod::compression_operation operation,
                        cnetmod::cancel_token& cancellation)
            -> cnetmod::task<std::expected<std::string, std::error_code>>
        {
            observation.dispatched = true;
            if (cancellation.is_cancelled())
                co_return std::unexpected(std::make_error_code(
                    std::errc::operation_canceled));
            co_return operation();
        },
        .measurements = [&observation](
                            cnetmod::instrumentation::metric_measurement value)
        {
            observation.measurements.push_back(std::move(value));
        },
    };
    if (!use_dispatch)
        options.dispatch = {};
    if (cancel)
        request.cancel_pending_operations();
    auto middleware = cnetmod::compress(std::move(options));
    cnetmod::sync_wait(middleware(request,
        [&request, &payload]() -> cnetmod::task<void>
        {
            request.text(cnetmod::http::status::ok, payload);
            co_return;
        }));
    observation.response = std::move(response);
    return observation;
}

#ifdef CNETMOD_HAS_ZLIB
auto gunzip(std::string_view compressed, std::size_t expected_size)
    -> std::optional<std::string>
{
    z_stream stream{};
    if (::inflateInit2(&stream, 15 + 16) != Z_OK)
        return std::nullopt;

    struct cleanup
    {
        z_stream* stream;

        ~cleanup()
        {
            ::inflateEnd(stream);
        }
    } guard{&stream};

    std::string output(expected_size, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (::inflate(&stream, Z_FINISH) != Z_STREAM_END)
        return std::nullopt;
    output.resize(static_cast<std::size_t>(stream.total_out));
    return output;
}
#endif
} // namespace

TEST(compression_honors_explicit_gzip_rejection)
{
    const auto observation = run_compression("br, gzip;q=0, *;q=1");
    ASSERT_FALSE(observation.dispatched);
    ASSERT_TRUE(observation.response.get_header("Content-Encoding").empty());
}

TEST(compression_accepts_case_insensitive_gzip_with_quality)
{
    const auto observation = run_compression("br;q=0.5, GZip; q=0.8");
#ifdef CNETMOD_HAS_ZLIB
    ASSERT_TRUE(observation.dispatched);
    ASSERT_EQ(observation.response.get_header("Content-Encoding"),
        std::string_view("gzip"));
    ASSERT_TRUE(observation.response.body().size() < 4096U);
#else
    ASSERT_FALSE(observation.dispatched);
#endif
}

TEST(compression_accepts_minimum_positive_quality)
{
    const auto observation = run_compression("gzip;q=0.001");
#ifdef CNETMOD_HAS_ZLIB
    ASSERT_TRUE(observation.dispatched);
#else
    ASSERT_FALSE(observation.dispatched);
#endif
}

TEST(compression_rejects_invalid_quality_syntax)
{
    ASSERT_FALSE(run_compression("gzip;q=.5").dispatched);
    ASSERT_FALSE(run_compression("gzip;q=0.0001").dispatched);
    ASSERT_FALSE(run_compression("gzip;q=1.1").dispatched);
    ASSERT_FALSE(run_compression("gzip;q=2").dispatched);
}

TEST(compression_uses_wildcard_when_gzip_is_not_explicit)
{
    const auto observation = run_compression("br;q=0, *;q=0.5");
#ifdef CNETMOD_HAS_ZLIB
    ASSERT_TRUE(observation.dispatched);
    ASSERT_EQ(observation.response.get_header("Content-Encoding"),
        std::string_view("gzip"));
#else
    ASSERT_FALSE(observation.dispatched);
#endif
}

TEST(compression_prefers_brotli_only_when_enabled_and_not_rated_lower)
{
    // Disabled by default: an equal-rated br is ignored.
    ASSERT_EQ(run_compression("gzip, br").response.get_header("Content-Encoding"),
        std::string_view(
#ifdef CNETMOD_HAS_ZLIB
            "gzip"
#else
            ""
#endif
            ));
#if defined(CNETMOD_HAS_ZLIB) && defined(CNETMOD_HAS_BROTLI)
    auto chosen = run_compression("gzip, br", {}, false, 2);
    ASSERT_EQ(chosen.response.get_header("Content-Encoding"), std::string_view("br"));
    std::string decoded(4096U, '\0');
    auto decoded_size = decoded.size();
    const auto body = chosen.response.body();
    ASSERT_TRUE(BrotliDecoderDecompress(body.size(),
                    reinterpret_cast<const std::uint8_t*>(body.data()), &decoded_size,
                    reinterpret_cast<std::uint8_t*>(decoded.data())) ==
        BROTLI_DECODER_RESULT_SUCCESS);
    ASSERT_EQ(decoded_size, 4096U);
    ASSERT_TRUE(decoded == std::string(4096U, 'a'));
    ASSERT_EQ(run_compression("br;q=0.5, gzip;q=0.8", {}, false, 2)
                  .response.get_header("Content-Encoding"),
        std::string_view("gzip"));
    ASSERT_EQ(run_compression("br;q=1, gzip;q=0.8", {}, false, 2)
                  .response.get_header("Content-Encoding"),
        std::string_view("br"));
    ASSERT_TRUE(run_compression("br;q=0, gzip;q=0", {}, false, 2)
            .response.get_header("Content-Encoding")
            .empty());
#endif
}

TEST(compression_merges_vary_and_publishes_measurements)
{
    const auto observation = run_compression("gzip", "Origin");
#ifdef CNETMOD_HAS_ZLIB
    ASSERT_EQ(observation.response.get_header("Vary"),
        std::string_view("Origin, Accept-Encoding"));
    ASSERT_TRUE(observation.measurements.size() >= 4U);
#else
    ASSERT_TRUE(observation.response.get_header("Vary").empty() ||
        observation.response.get_header("Vary") == "Origin");
#endif
}

TEST(compression_does_not_dispatch_cancelled_request)
{
    const auto observation = run_compression("gzip", {}, true);
    ASSERT_FALSE(observation.dispatched);
    ASSERT_TRUE(observation.response.get_header("Content-Encoding").empty());
}

TEST(compression_reuses_gzip_workspace_without_retaining_previous_input)
{
#ifdef CNETMOD_HAS_ZLIB
    for (const auto fill : {'a', 'b', 'c', 'a'})
    {
        const auto payload = std::string(8192U, fill);
        const auto observation = run_compression(
            "gzip", {}, false, -1, payload);
        ASSERT_EQ(observation.response.get_header("Content-Encoding"),
            std::string_view("gzip"));
        const auto decoded = gunzip(observation.response.body(), payload.size());
        ASSERT_TRUE(decoded.has_value());
        ASSERT_TRUE(*decoded == payload);
    }
#endif
}

TEST(compression_can_transform_response_storage_without_a_dispatcher)
{
#ifdef CNETMOD_HAS_ZLIB
    const auto payload = std::string(8192U, 'x');
    const auto observation = run_compression(
        "gzip", {}, false, -1, payload, false);
    ASSERT_FALSE(observation.dispatched);
    ASSERT_EQ(observation.response.get_header("Content-Encoding"),
        std::string_view("gzip"));
    const auto decoded = gunzip(observation.response.body(), payload.size());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(*decoded == payload);
#endif
}

TEST(compression_reuses_only_an_explicitly_keyed_unchanged_response)
{
#ifdef CNETMOD_HAS_ZLIB
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::header_map headers;
    headers["Accept-Encoding"] = "gzip";
    std::size_t operations{};
    auto middleware = cnetmod::compress({
        .min_size = 1,
        .max_concurrency = 1,
        .cache_capacity = 2,
        .cache_key = [](const cnetmod::http::request_context&)
            -> std::optional<std::string>
        {
            return "stable-resource";
        },
        .measurements = [&operations](
                            cnetmod::instrumentation::metric_measurement value)
        {
            if (value.name == "http.server.response.compression.operations" &&
                std::ranges::any_of(value.attributes, [](const auto& attribute)
                    {
                        return attribute.first == "outcome" &&
                            attribute.second == "compressed";
                    }))
                ++operations;
        },
    });

    const auto invoke = [&](std::string payload)
    {
        cnetmod::http::response response;
        cnetmod::http::request_context request{
            *io, socket, "GET", "/", headers, {}, response, {}};
        cnetmod::sync_wait(middleware(request,
            [&request, payload = std::move(payload)]() -> cnetmod::task<void>
            {
                request.text(cnetmod::http::status::ok, payload);
                co_return;
            }));
        return response;
    };

    const auto original = std::string(8192U, 'a');
    auto first = invoke(original);
    auto second = invoke(original);
    ASSERT_EQ(operations, 1U);
    ASSERT_TRUE(first.body() == second.body());

    const auto changed = std::string(8192U, 'b');
    auto third = invoke(changed);
    ASSERT_EQ(operations, 2U);
    const auto decoded = gunzip(third.body(), changed.size());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(*decoded == changed);
#endif
}

TEST(compression_ignores_a_throwing_application_cache_key)
{
#ifdef CNETMOD_HAS_ZLIB
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::header_map headers;
    headers["Accept-Encoding"] = "gzip";
    auto middleware = cnetmod::compress({
        .min_size = 1,
        .max_concurrency = 1,
        .cache_capacity = 1,
        .cache_key = [](const cnetmod::http::request_context&)
            -> std::optional<std::string>
        {
            throw std::runtime_error{"application cache-key failure"};
        },
    });

    const auto payload = std::string(8192U, 'x');
    cnetmod::http::response response;
    cnetmod::http::request_context request{
        *io, socket, "GET", "/", headers, {}, response, {}};
    cnetmod::sync_wait(middleware(request,
        [&request, &payload]() -> cnetmod::task<void>
        {
            request.text(cnetmod::http::status::ok, payload);
            co_return;
        }));

    ASSERT_EQ(response.get_header("Content-Encoding"),
        std::string_view("gzip"));
    const auto decoded = gunzip(response.body(), payload.size());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(*decoded == payload);
#endif
}

RUN_TESTS()
