module cnetmod.protocol.http.middleware.rate_limiter;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {
namespace {
struct bucket {
  double tokens;
  std::chrono::steady_clock::time_point last_refill;
};
struct rate_limiter_state {
  std::mutex mutex;
  std::unordered_map<std::string, bucket> buckets;
  std::chrono::steady_clock::time_point last_gc =
      std::chrono::steady_clock::now();
};
auto limit_request(const rate_limiter_options &opts, rate_limiter_state &state,
                   http::request_context &ctx, http::next_fn next)
    -> task<void> {
  const auto key = opts.key_fn(ctx);
  const auto now = std::chrono::steady_clock::now();
  bool allowed = false;
  double retry_after = 0.0;
  {
    std::lock_guard lock(state.mutex);
    if (now - state.last_gc > std::chrono::seconds{60}) {
      state.last_gc = now;
      for (auto it = state.buckets.begin(); it != state.buckets.end();) {
        if (now - it->second.last_refill > opts.entry_ttl)
          it = state.buckets.erase(it);
        else
          ++it;
      }
    }
    auto [it, inserted] =
        state.buckets.try_emplace(key, bucket{opts.burst, now});
    auto &current = it->second;
    const auto elapsed =
        std::chrono::duration<double>(now - current.last_refill).count();
    current.tokens = std::min(opts.burst, current.tokens + elapsed * opts.rate);
    current.last_refill = now;
    if (current.tokens >= 1.0) {
      current.tokens -= 1.0;
      allowed = true;
    } else
      retry_after = opts.rate > 0.0 ? (1.0 - current.tokens) / opts.rate : 1.0;
  }
  if (!allowed) {
    const auto seconds = static_cast<int>(std::ceil(retry_after));
    ctx.resp().set_header("Retry-After", std::to_string(seconds));
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
auto rate_limiter(rate_limiter_options opts) -> http::middleware_fn {
  if (!opts.key_fn)
    opts.key_fn = [](http::request_context &ctx) {
      return http::resolve_client_ip(ctx, "global");
    };
  auto state = std::make_shared<rate_limiter_state>();
  return [opts = std::move(opts), state = std::move(state)](
             http::request_context &ctx, http::next_fn next) -> task<void> {
    return limit_request(opts, *state, ctx, std::move(next));
  };
}
} // namespace cnetmod
