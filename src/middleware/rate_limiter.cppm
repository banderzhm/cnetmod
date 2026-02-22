/**
 * @file rate_limiter.cppm
 * @brief Generic Rate Limiting Middleware — Token Bucket Algorithm
 *
 * O(1) decision, supports burst traffic, auto-reclaims inactive entries.
 * Defaults to rate limiting by client IP, customizable key extraction function.
 *
 * Usage example:
 *   import cnetmod.middleware.rate_limiter;
 *
 *   // Default: 10 req/s, burst 20
 *   svr.use(rate_limiter());
 *
 *   // Custom
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
// rate_limiter_options — Rate Limiting Configuration
// =============================================================================

export struct rate_limiter_options {
    double rate  = 10.0;   ///< Tokens replenished per second
    double burst = 20.0;   ///< Bucket capacity (max burst)

    /// Key extraction function: Extract rate limiting dimension from request context (default: client IP)
    std::function<std::string(http::request_context&)> key_fn;

    /// Idle entry expiration time (buckets with no requests for long time are auto-reclaimed)
    std::chrono::seconds entry_ttl{300};
};

// =============================================================================
// rate_limiter — Token Bucket Rate Limiting Middleware
// =============================================================================
//
// Algorithm:
//   tokens = min(burst, tokens + elapsed * rate)
//   if tokens >= 1: consume, allow
//   else: reject 429 + Retry-After
//
// Thread safety: std::mutex (critical section extremely short, no co_await)
// Memory management: Lazy GC every 60s clears buckets inactive beyond entry_ttl

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

    // Default key: Client IP (X-Forwarded-For > X-Real-IP > "global")
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

            // Lazy GC: Clean up every 60s
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

            // Replenish tokens
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
