/// cnetmod.coro.rate_limiter — Thread-safe token bucket admission control

module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.rate_limiter;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod {

export struct rate_limit
{
    double tokens_per_second = 0.0;
    double burst = 0.0;
};

/// Synchronous, non-blocking token bucket suitable for coroutine admission.
export class token_bucket
{
public:
    explicit token_bucket(rate_limit limit = {}) noexcept;
    [[nodiscard]] auto try_consume(double tokens = 1.0) noexcept -> bool;
    [[nodiscard]] auto limit() const noexcept -> rate_limit;
    void reset() noexcept;

private:
    mutable concurrent_containers::atomic_rw_latch latch_;
    rate_limit limit_;
    double tokens_ = 0.0;
    std::chrono::steady_clock::time_point last_refill_;
};

} // namespace cnetmod
