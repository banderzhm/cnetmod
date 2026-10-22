#include "test_framework.hpp"
#include <cnetmod/config.hpp>
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
    std::string vary = {}, bool cancel = false, int brotli_quality = -1)
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
    if (cancel)
        request.cancel_pending_operations();
    auto middleware = cnetmod::compress(std::move(options));
    cnetmod::sync_wait(middleware(request,
        [&request]() -> cnetmod::task<void>
        {
            request.text(cnetmod::http::status::ok,
                std::string(4096, 'a'));
            co_return;
        }));
    observation.response = std::move(response);
    return observation;
}
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
                    .response.get_header("Content-Encoding").empty());
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

RUN_TESTS()
