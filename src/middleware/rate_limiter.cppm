/**
 * @file rate_limiter.cppm
 * @brief 通用限流中间件 — Token Bucket 算法
 *
 * O(1) 判定，支持突发流量，自动回收不活跃条目。
 * 默认按客户端 IP 限流，可自定义 key 提取函数。
 *
 * 使用示例:
 *   import cnetmod.middleware.rate_limiter;
 *
 *   // 默认: 10 req/s, 突发 20
 *   svr.use(rate_limiter());
 *
 *   // 自定义
 *   svr.use(rate_limiter({
 *       .rate = 5.0,
 *       .burst = 10.0,
 *       .key_fn = [](auto& ctx) { return std::string(ctx.get_header("Authorization")); },
 *   }));
 */
export module cnetmod.middleware.rate_limiter;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// rate_limiter_options — 限流配置
// =============================================================================

export struct rate_limiter_options {
    double rate  = 10.0;   ///< 每秒补充令牌数
    double burst = 20.0;   ///< 桶容量 (最大突发)

    /// Key 提取函数: 从请求上下文提取限流维度 (默认: 客户端 IP)
    std::function<std::string(http::request_context&)> key_fn;

    /// 空闲条目过期时间 (长时间无请求的桶自动回收)
    std::chrono::seconds entry_ttl{300};
};

// =============================================================================
// rate_limiter — Token Bucket 限流中间件
// =============================================================================
//
// 算法:
//   tokens = min(burst, tokens + elapsed * rate)
//   if tokens >= 1: consume, allow
//   else: reject 429 + Retry-After
//
// 线程安全: std::mutex (临界区极短, 无 co_await)
// 内存管理: 每 60s lazy GC 清理超过 entry_ttl 未活跃的桶

export inline auto rate_limiter(rate_limiter_options opts = {})
    -> http::middleware_fn
{
    struct bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    struct state {
        std::mutex mtx;
        std::unordered_map<std::string, bucket> buckets;
        std::chrono::steady_clock::time_point last_gc;
    };

    auto st = std::make_shared<state>();
    st->last_gc = std::chrono::steady_clock::now();

    // 默认 key: 客户端 IP (X-Forwarded-For > X-Real-IP > "global")
    if (!opts.key_fn) {
        opts.key_fn = [](http::request_context& ctx) -> std::string {
            return http::resolve_client_ip(ctx, "global");
        };
    }

    return [opts = std::move(opts), st]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        auto key = opts.key_fn(ctx);
        auto now = std::chrono::steady_clock::now();

        bool allowed = false;
        double retry_after = 0;

        {
            std::lock_guard lock(st->mtx);

            // Lazy GC: 每 60s 清理一次
            if ((now - st->last_gc) > std::chrono::seconds{60}) {
                st->last_gc = now;
                for (auto it = st->buckets.begin(); it != st->buckets.end(); ) {
                    if ((now - it->second.last_refill) > opts.entry_ttl)
                        it = st->buckets.erase(it);
                    else
                        ++it;
                }
            }

            auto [it, inserted] = st->buckets.try_emplace(
                key, bucket{opts.burst, now});
            auto& b = it->second;

            // 补充令牌
            auto elapsed = std::chrono::duration<double>(
                now - b.last_refill).count();
            b.tokens = std::min(opts.burst, b.tokens + elapsed * opts.rate);
            b.last_refill = now;

            if (b.tokens >= 1.0) {
                b.tokens -= 1.0;
                allowed = true;
            } else {
                retry_after = (1.0 - b.tokens) / opts.rate;
            }
        }

        if (!allowed) {
            auto ra = static_cast<int>(std::ceil(retry_after));
            ctx.resp().set_header("Retry-After", std::to_string(ra));
            ctx.json(http::status::too_many_requests, std::format(
                R"({{"error":"rate limit exceeded","retry_after_seconds":{}}})",
                ra));
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
