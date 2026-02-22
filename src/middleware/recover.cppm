/**
 * @file recover.cppm
 * @brief Exception Recovery Middleware — Catch unhandled handler exceptions, return 500 and log
 *
 * Prevents handler exceptions from causing connection drops without response.
 * Should be placed at outermost layer of middleware chain to catch all exceptions.
 *
 * Usage example:
 *   import cnetmod.middleware.recover;
 *
 *   svr.use(recover());  // Outermost layer
 *   svr.use(access_log());
 *   svr.use(cors());
 *   // ...
 */
export module cnetmod.middleware.recover;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// recover — Exception Recovery Middleware
// =============================================================================
//
// Behavior:
//   try { co_await next(); }
//   catch (std::exception) → 500 + error log
//   catch (...)            → 500 + error log

export inline auto recover() -> http::middleware_fn
{
    return [](http::request_context& ctx, http::next_fn next) -> task<void> {
        try {
            co_await next();
        } catch (const std::exception& e) {
            logger::error("{} {} - unhandled exception: {}",
                          ctx.method(), ctx.path(), e.what());
            ctx.json(http::status::internal_server_error,
                R"({"error":"internal server error"})");
        } catch (...) {
            logger::error("{} {} - unknown exception",
                          ctx.method(), ctx.path());
            ctx.json(http::status::internal_server_error,
                R"({"error":"internal server error"})");
        }
    };
}

} // namespace cnetmod
