/**
 * @file request_id.cppm
 * @brief 请求追踪 ID 中间件 — 生成/传播 X-Request-ID
 *
 * 每个请求分配唯一 ID，写入响应头。若请求已携带 X-Request-ID (反代链路)，
 * 则复用该 ID，实现分布式链路追踪。
 *
 * 使用示例:
 *   import cnetmod.middleware.request_id;
 *
 *   svr.use(request_id());
 *   // handler 中可通过 ctx.resp().get_header("X-Request-ID") 获取当前请求 ID
 */
export module cnetmod.middleware.request_id;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

namespace detail {

/// 生成 128-bit 随机 hex ID (CSPRNG)
inline auto generate_request_id() -> std::string {
    static thread_local std::random_device rd;
    static constexpr char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 16; ++i) {
        auto byte = static_cast<std::uint8_t>(rd() & 0xFF);
        id.push_back(hex[(byte >> 4) & 0x0F]);
        id.push_back(hex[byte & 0x0F]);
    }
    return id;
}

} // namespace detail

// =============================================================================
// request_id — 请求追踪 ID 中间件
// =============================================================================
//
// 行为:
//   1. 请求已有 X-Request-ID → 复用 (反代/网关传入)
//   2. 没有 → 生成 128-bit 随机 hex
//   3. 写入响应头 X-Request-ID
//   4. handler 通过 ctx.resp().get_header("X-Request-ID") 读取

export inline auto request_id(std::string_view header_name = "X-Request-ID")
    -> http::middleware_fn
{
    return [hdr = std::string(header_name)]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        auto existing = ctx.get_header(hdr);
        auto id = existing.empty()
                  ? detail::generate_request_id()
                  : std::string(existing);

        ctx.resp().set_header(hdr, id);

        co_await next();
    };
}

} // namespace cnetmod
