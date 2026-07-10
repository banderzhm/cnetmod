module cnetmod.coro.cancel;

import std;

namespace cnetmod
{
    void cancel_token::cancel() noexcept
    {
        if (cancelled_.exchange(true, std::memory_order_acq_rel)) return;
        if (pending_.load(std::memory_order_acquire) && cancel_fn_) cancel_fn_(*this);
    }

    auto cancel_token::is_cancelled() const noexcept -> bool
    {
        return cancelled_.load(std::memory_order_acquire);
    }

    void cancel_token::reset() noexcept
    {
        cancelled_.store(false, std::memory_order_relaxed);
        pending_.store(false, std::memory_order_relaxed);
        cancel_fn_ = nullptr;
        ctx_ = nullptr;
        io_handle_ = nullptr;
        overlapped_ = nullptr;
        fd_ = -1;
        filter_ = 0;
        coroutine_ = {};
    }
} // namespace cnetmod