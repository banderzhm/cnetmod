/**
 * @file timeout.cppm
 * @brief Request Timeout Middleware — Detects slow requests and overrides with 504 Gateway Timeout
 *
 * After calling next(), checks elapsed time, if exceeds threshold:
 *   1. Override response to 504 Gateway Timeout
 *   2. Output warn level log
 *
 * Note: This is a "soft timeout" — cannot interrupt executing handler,
 * but ensures client receives timeout response instead of late success response.
 * Should be placed in outer layer of middleware chain (after recover),
 * so handler's response can be overridden after it returns.
 *
 * Usage Example:
 *   import cnetmod.middleware.timeout;
 *
 *   svr.use(recover());
 *   svr.use(request_timeout(std::chrono::seconds{5}));
 *   svr.use(access_log());
 */
export module cnetmod.middleware.timeout;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// request_timeout — Request timeout middleware
// =============================================================================

export inline auto request_timeout(std::chrono::steady_clock::duration max_time)
    -> http::middleware_fn
{
    return [max_time](http::request_context& ctx, http::next_fn next) -> task<void> {
        auto start = std::chrono::steady_clock::now();

        co_await next();

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > max_time) {
            auto ms = std::chrono::duration<double, std::milli>(elapsed).count();
            auto limit_ms = std::chrono::duration<double, std::milli>(max_time).count();

            logger::warn("{} {} exceeded timeout ({:.0f}ms > {:.0f}ms)",
                         ctx.method(), ctx.path(), ms, limit_ms);

            // Override handler's response to 504
            ctx.json(http::status::gateway_timeout, std::format(
                R"({{"error":"request timeout","elapsed_ms":{:.0f},"limit_ms":{:.0f}}})",
                ms, limit_ms));
        }
    };
}

} // namespace cnetmod
