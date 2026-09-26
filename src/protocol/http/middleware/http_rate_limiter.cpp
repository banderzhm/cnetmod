module cnetmod.protocol.http.middleware.rate_limiter;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.utils.concurrent_containers.striped_hash_map;

namespace cnetmod {
namespace {
    struct bucket
    {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    struct rate_limiter_state
    {
        // One key is updated under one stripe-local CAS latch. This avoids a
        // global limiter mutex turning unrelated client addresses into a
        // single contention domain.
        concurrent_containers::striped_hash_map<std::string, bucket> buckets{
            256U};
        std::atomic<std::int64_t> last_gc_seconds{};
    };

    auto limit_request(const rate_limiter_options& opts, rate_limiter_state& state,
        http::request_context& ctx, http::next_fn next)
        -> task<void>
    {
        const auto key = opts.key_fn(ctx);
        const auto now = std::chrono::steady_clock::now();
        const auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch())
                                     .count();
        auto previous_gc = state.last_gc_seconds.load(std::memory_order_acquire);
        if (now_seconds - previous_gc >= 60 &&
            state.last_gc_seconds.compare_exchange_strong(previous_gc, now_seconds,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            (void)state.buckets.erase_if([&](const std::string&, const bucket& current)
                {
                    return now - current.last_refill > opts.entry_ttl;
                });
        }
        const auto decision = state.buckets.update_or_emplace(key,
            bucket{opts.burst, now}, [&](bucket& current, bool inserted)
            {
                if (!inserted)
                {
                    const auto elapsed =
                        std::chrono::duration<double>(now - current.last_refill).count();
                    current.tokens = std::min(opts.burst, current.tokens + elapsed * opts.rate);
                    current.last_refill = now;
                }
                if (current.tokens >= 1.0)
                {
                    current.tokens -= 1.0;
                    return std::pair{true, 0.0};
                }
                const auto retry_after = opts.rate > 0.0
                    ? (1.0 - current.tokens) / opts.rate
                    : 1.0;
                return std::pair{false, retry_after};
            });
        if (!decision.first)
        {
            const auto seconds = std::max(1, static_cast<int>(std::ceil(decision.second)));
            ctx.resp().set_header("Retry-After", std::to_string(seconds));
            if (opts.on_limited)
            {
                opts.on_limited(ctx, std::chrono::seconds{seconds});
                co_return;
            }
            ctx.json(
                http::status::too_many_requests,
                std::format(
                    R"({{"error":"rate limit exceeded","retry_after_seconds":{}}})",
                    seconds));
            co_return;
        }
        co_await next();
    }
} // namespace

auto rate_limiter(rate_limiter_options opts) -> http::middleware_fn
{
    if (!opts.key_fn)
        opts.key_fn = [proxies = opts.trusted_proxies](http::request_context& ctx)
        {
            return http::resolve_client_ip(ctx, proxies);
        };
    auto state = std::make_shared<rate_limiter_state>();
    return [opts = std::move(opts), state = std::move(state)](
               http::request_context& ctx, http::next_fn next) -> task<void>
    {
        return limit_request(opts, *state, ctx, std::move(next));
    };
}
} // namespace cnetmod
