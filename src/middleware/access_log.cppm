/**
 * @file access_log.cppm
 * @brief HTTP / WebSocket Aspect Tool — API Latency Calculation and Access Logging
 *
 * Provides two aspects:
 *
 * 1. access_log() → http::middleware_fn
 *    HTTP request latency logging middleware (onion model), records method, path, status, latency.
 *
 * 2. ws_access_log(handler) → ws::ws_handler_fn
 *    WebSocket connection latency logging wrapper, records path, connection duration.
 *
 * Usage example:
 *   import cnetmod.middleware.aspect;
 *
 *   // HTTP
 *   http::server svr(ctx);
 *   svr.use(access_log());                         // Global middleware
 *   svr.use(access_log(logger::level::debug));      // Specify log level
 *
 *   // WebSocket
 *   ws::server ws_svr(ctx);
 *   ws_svr.on("/ws/chat", ws_access_log([](ws::ws_context& ctx) -> task<void> {
 *       // ...
 *   }));
 */
export module cnetmod.middleware.access_log;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// access_log — HTTP API Latency Logging Middleware
// =============================================================================
//
// Onion model: Timing before and after handler
//
// Log format:
//   [INFO] GET /api/users 200 3.25ms
//   [WARN] POST /api/login 500 120.40ms   (5xx auto-upgraded to warn)
//

/// Create HTTP access log middleware
/// @param lv  Log level for normal requests (5xx auto-upgraded to warn/error)
/// @param loc Auto-capture call site location (line where svr.use(access_log()) is called)
export inline auto access_log(logger::level lv = logger::level::info,
    std::source_location loc = std::source_location::current())
    -> http::middleware_fn
{
    return [lv, loc](http::request_context& ctx, http::next_fn next) -> task<void> {
        auto start = std::chrono::steady_clock::now();

        co_await next();

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration<double, std::milli>(elapsed).count();

        auto status = ctx.resp().status_code();
        auto method = ctx.method();
        auto path   = ctx.path();

        // 5xx → warn, 4xx → keep original level, others → lv
        auto log_lv = lv;
        if (status >= 500)      log_lv = logger::level::warn;
        else if (status >= 400) log_lv = std::max(lv, logger::level::info);

        logger::detail::write_log(log_lv,
            std::format("{} {} {} {:.2f}ms", method, path, status, ms), loc);
    };
}

// =============================================================================
// ws_access_log — WebSocket Connection Latency Logging Wrapper
// =============================================================================
//
// Wraps a ws_handler_fn, prints logs on connection establish/disconnect:
//   [INFO] WS+ /ws/chat            — Connection established
//   [INFO] WS- /ws/chat 45230.12ms — Connection closed + duration
//

/// Wrap WebSocket handler, add connection lifecycle logging
/// @param handler Original handler
/// @param lv  Log level
/// @param loc Auto-capture call site location
export inline auto ws_access_log(ws::ws_handler_fn handler,
                                 logger::level lv = logger::level::info,
                                 std::source_location loc = std::source_location::current())
    -> ws::ws_handler_fn
{
    return [handler = std::move(handler), lv, loc]
           (ws::ws_context& ctx) -> task<void>
    {
        auto path = std::string(ctx.path());

        logger::detail::write_log(lv, std::format("WS+ {}", path), loc);

        auto start = std::chrono::steady_clock::now();

        try {
            co_await handler(ctx);
        } catch (...) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            auto ms = std::chrono::duration<double, std::milli>(elapsed).count();
            logger::detail::write_log(logger::level::error,
                std::format("WS! {} {:.2f}ms (exception)", path, ms), loc);
            throw;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration<double, std::milli>(elapsed).count();
        logger::detail::write_log(lv,
            std::format("WS- {} {:.2f}ms", path, ms), loc);
    };
}

} // namespace cnetmod
