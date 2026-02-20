/**
 * @file access_log.cppm
 * @brief HTTP / WebSocket 切面工具 — 接口耗时计算与访问日志
 *
 * 提供两个切面:
 *
 * 1. access_log() → http::middleware_fn
 *    HTTP 请求耗时日志中间件（洋葱模型），记录 method、path、status、耗时。
 *
 * 2. ws_access_log(handler) → ws::ws_handler_fn
 *    WebSocket 连接耗时日志包装器，记录 path、连接持续时间。
 *
 * 使用示例:
 *   import cnetmod.middleware.aspect;
 *
 *   // HTTP
 *   http::server svr(ctx);
 *   svr.use(access_log());                         // 全局中间件
 *   svr.use(access_log(logger::level::debug));      // 指定日志级别
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
// access_log — HTTP 接口耗时日志中间件
// =============================================================================
//
// 洋葱模型: 在 handler 前后打点计时
//
// 日志格式:
//   [INFO] GET /api/users 200 3.25ms
//   [WARN] POST /api/login 500 120.40ms   (5xx 自动升级为 warn)
//

/// 创建 HTTP 访问日志中间件
/// @param lv  正常请求的日志级别（5xx 会自动升级为 warn/error）
/// @param loc 自动捕获调用处位置（即 svr.use(access_log()) 所在行）
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

        // 5xx → warn, 4xx → 保持原级别, 其余 → lv
        auto log_lv = lv;
        if (status >= 500)      log_lv = logger::level::warn;
        else if (status >= 400) log_lv = std::max(lv, logger::level::info);

        logger::detail::write_log(log_lv,
            std::format("{} {} {} {:.2f}ms", method, path, status, ms), loc);
    };
}

// =============================================================================
// ws_access_log — WebSocket 连接耗时日志包装器
// =============================================================================
//
// 包裹一个 ws_handler_fn，在连接建立/断开时打印日志:
//   [INFO] WS+ /ws/chat            — 连接建立
//   [INFO] WS- /ws/chat 45230.12ms — 连接断开 + 持续时间
//

/// 包装 WebSocket handler，添加连接生命周期日志
/// @param handler 原始 handler
/// @param lv  日志级别
/// @param loc 自动捕获调用处位置
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
