/**
 * @file recover.cppm
 * @brief 异常恢复中间件 — 捕获 handler 未处理异常，返回 500 并记录日志
 *
 * 防止 handler 抛异常导致连接直接断开且无响应。
 * 应放在中间件链最外层，确保所有异常都能被捕获。
 *
 * 使用示例:
 *   import cnetmod.middleware.recover;
 *
 *   svr.use(recover());  // 最外层
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
// recover — 异常恢复中间件
// =============================================================================
//
// 行为:
//   try { co_await next(); }
//   catch (std::exception) → 500 + 错误日志
//   catch (...)            → 500 + 错误日志

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
