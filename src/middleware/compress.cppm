/**
 * @file compress.cppm
 * @brief gzip response compression middleware — auto-compress text-type responses
 *
 * Checks Accept-Encoding, performs gzip compression on text-type MIME responses with body exceeding threshold.
 * Requires zlib support (CNETMOD_HAS_ZLIB), degrades to pass-through when not enabled.
 *
 * Usage example:
 *   import cnetmod.middleware.compress;
 *
 *   svr.use(compress());                             // Default: 1KB threshold
 *   svr.use(compress({.min_size = 256}));             // Custom threshold
 *   svr.use(compress({.level = 6}));                  // Compression level 1-9
 */
module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_ZLIB
#include <zlib.h>
#endif

export module cnetmod.middleware.compress;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// compress_options — Compression configuration
// =============================================================================

export struct compress_options {
    std::size_t min_size = 1024;   ///< Don't compress if response body smaller than this (bytes)
    int level = 6;                 ///< gzip compression level (1=fastest, 9=best, 6=default)
};

// =============================================================================
// Internal utilities
// =============================================================================

namespace detail {

/// Determine if Content-Type is compressible text type
inline auto is_compressible(std::string_view content_type) -> bool {
    // All text/* are compressible
    if (content_type.starts_with("text/")) return true;
    // JSON / XML / JavaScript / SVG
    if (content_type.find("json") != std::string_view::npos) return true;
    if (content_type.find("xml") != std::string_view::npos) return true;
    if (content_type.find("javascript") != std::string_view::npos) return true;
    if (content_type.find("svg") != std::string_view::npos) return true;
    return false;
}

/// Check if Accept-Encoding contains gzip
inline auto accepts_gzip(std::string_view accept_encoding) -> bool {
    // Simple match: contains "gzip"
    return accept_encoding.find("gzip") != std::string_view::npos;
}

#ifdef CNETMOD_HAS_ZLIB

/// gzip compression (zlib)
inline auto gzip_compress(std::string_view input, int level)
    -> std::optional<std::string>
{
    // gzip compression upper bound estimate
    auto bound = ::compressBound(static_cast<uLong>(input.size()));
    // gzip header/footer extra overhead
    std::vector<Bytef> out(bound + 32);

    z_stream strm{};
    // windowBits = 15 + 16 enables gzip format
    if (deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return std::nullopt;

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    strm.avail_in = static_cast<uInt>(input.size());
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());

    auto ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (ret != Z_STREAM_END)
        return std::nullopt;

    auto compressed_size = out.size() - strm.avail_out;
    return std::string(reinterpret_cast<const char*>(out.data()), compressed_size);
}

#endif // CNETMOD_HAS_ZLIB

} // namespace detail

// =============================================================================
// compress — gzip response compression middleware
// =============================================================================
//
// Flow:
//   1. Execute next() to get handler response
//   2. Check: Accept-Encoding contains gzip? Content-Type compressible? body > min_size?
//   3. All satisfied → gzip compress body, set Content-Encoding: gzip
//   4. Any not satisfied → pass through

export inline auto compress(compress_options opts = {}) -> http::middleware_fn
{
    return [opts](http::request_context& ctx, http::next_fn next) -> task<void> {
        co_await next();

#ifdef CNETMOD_HAS_ZLIB
        // Check if client accepts gzip
        auto ae = ctx.get_header("Accept-Encoding");
        if (!detail::accepts_gzip(ae))
            co_return;

        // Check if response is already compressed
        auto ce = ctx.resp().get_header("Content-Encoding");
        if (!ce.empty())
            co_return;

        // Check if Content-Type is compressible
        auto ct = ctx.resp().get_header("Content-Type");
        if (!detail::is_compressible(ct))
            co_return;

        // Check body size
        auto body = ctx.resp().body();
        if (body.size() < opts.min_size)
            co_return;

        // Perform compression
        auto compressed = detail::gzip_compress(body, opts.level);
        if (!compressed || compressed->size() >= body.size())
            co_return;  // Compression ineffective

        // Replace body
        ctx.resp().set_body(std::move(*compressed));
        ctx.resp().set_header("Content-Encoding", "gzip");
        ctx.resp().set_header("Content-Length",
            std::to_string(ctx.resp().body().size()));
        // Vary: Accept-Encoding — notify cache proxies
        ctx.resp().set_header("Vary", "Accept-Encoding");
#else
        // zlib not enabled, pass-through
        (void)opts;
#endif
    };
}

} // namespace cnetmod
