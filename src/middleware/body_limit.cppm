/**
 * @file body_limit.cppm
 * @brief 请求体大小限制中间件 — 超限返回 413 Payload Too Large
 *
 * 检查 Content-Length 和实际 body 大小，在 handler 执行前拒绝过大请求，防止 OOM。
 *
 * 使用示例:
 *   import cnetmod.middleware.body_limit;
 *
 *   svr.use(body_limit());                     // 默认 1MB
 *   svr.use(body_limit(8 * 1024 * 1024));      // 8MB
 */
export module cnetmod.middleware.body_limit;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// body_limit — 请求体大小限制中间件
// =============================================================================
//
// 检查顺序:
//   1. Content-Length 头 (如果存在) > max_bytes → 413
//   2. 实际 body.size() > max_bytes → 413 (兜底，处理 chunked 传输)
//   3. 通过 → 调用 next()

export inline auto body_limit(std::size_t max_bytes = 1024 * 1024)
    -> http::middleware_fn
{
    return [max_bytes]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        // 检查 Content-Length 头
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

        // 兜底: 检查实际 body 大小
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
