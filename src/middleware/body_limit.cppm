/**
 * @file body_limit.cppm
 * @brief Request body size limit middleware — returns 413 Payload Too Large when exceeded
 *
 * Checks Content-Length and actual body size, rejects oversized requests before handler execution to prevent OOM.
 *
 * Usage example:
 *   import cnetmod.middleware.body_limit;
 *
 *   svr.use(body_limit());                     // Default 1MB
 *   svr.use(body_limit(8 * 1024 * 1024));      // 8MB
 */
export module cnetmod.middleware.body_limit;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// body_limit — Request body size limit middleware
// =============================================================================
//
// Check order:
//   1. Content-Length header (if exists) > max_bytes → 413
//   2. Actual body.size() > max_bytes → 413 (fallback, handles chunked transfer)
//   3. Pass → call next()

export inline auto body_limit(std::size_t max_bytes = 1024 * 1024)
    -> http::middleware_fn
{
    return [max_bytes]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        // Check Content-Length header
        auto cl = ctx.get_header("Content-Length");
        if (!cl.empty()) {
            std::size_t len = 0;
            auto [ptr, ec] = std::from_chars(
                cl.data(), cl.data() + cl.size(), len);
            if (ec == std::errc{} && len > max_bytes) {
                ctx.json(http::status::payload_too_large, std::format(
                    R"({{"error":"request body too large","limit":{},"size":{}}})",
                    max_bytes, len));
                co_return;
            }
        }

        // Fallback: check actual body size
        if (ctx.body().size() > max_bytes) {
            ctx.json(http::status::payload_too_large, std::format(
                R"({{"error":"request body too large","limit":{},"size":{}}})",
                max_bytes, ctx.body().size()));
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
