/**
 * @file timeout.cppm
 * @brief 请求超时中间件 — 检测慢请求并覆盖为 504 Gateway Timeout
 *
 * 调用 next() 后检测耗时，若超过阈值则：
 *   1. 覆盖响应为 504 Gateway Timeout
 *   2. 输出 warn 级别日志
 *
 * 注意：此为「软超时」— 不能中断正在执行的 handler，
 * 但能确保客户端收到超时响应而非超时成功响应。
 * 应放在中间件链靠外层（recover 之后），
 * 使 handler 返回后能覆盖其响应。
 *
 * 使用示例:
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
// request_timeout — 请求超时中间件
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

            // 覆盖 handler 的响应为 504
            ctx.json(http::status::gateway_timeout, std::format(
                R"({{"error":"request timeout","elapsed_ms":{:.0f},"limit_ms":{:.0f}}})",
                ms, limit_ms));
        }
    };
}

} // namespace cnetmod
