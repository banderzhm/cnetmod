/// cnetmod.coro.rate_limiter — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.coro.rate_limiter;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod {

token_bucket::token_bucket(rate_limit limit) noexcept
    : limit_(limit), tokens_(std::max(0.0, limit.burst)), last_refill_(std::chrono::steady_clock::now())
{
}

auto token_bucket::try_consume(double tokens) noexcept -> bool
{
    if (!(tokens > 0.0) || !(limit_.tokens_per_second > 0.0) ||
        !(limit_.burst > 0.0))
        return false;
    concurrent_containers::exclusive_latch_guard guard{latch_};
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration<double>(now - last_refill_).count();
    tokens_ = std::min(limit_.burst,
        tokens_ + std::max(0.0, elapsed) * limit_.tokens_per_second);
    last_refill_ = now;
    if (tokens_ < tokens)
        return false;
    tokens_ -= tokens;
    return true;
}

auto token_bucket::limit() const noexcept -> rate_limit
{
    concurrent_containers::shared_latch_guard guard{latch_};
    return limit_;
}

void token_bucket::reset() noexcept
{
    concurrent_containers::exclusive_latch_guard guard{latch_};
    tokens_ = std::max(0.0, limit_.burst);
    last_refill_ = std::chrono::steady_clock::now();
}

} // namespace cnetmod
