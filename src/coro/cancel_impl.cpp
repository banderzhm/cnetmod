module cnetmod.coro.cancel;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod {
void cancel_token::cancel() noexcept
{
    cancellation_reason expected = cancellation_reason::none;
    if (!reason_.compare_exchange_strong(expected, cancellation_reason::caller_cancelled,
            std::memory_order_acq_rel))
        return;
    if (cancelled_.exchange(true, std::memory_order_acq_rel))
        return;
    (void)dispatch_callback();
}

void cancel_token::cancel_due_to_deadline() noexcept
{
    cancellation_reason expected = cancellation_reason::none;
    if (!reason_.compare_exchange_strong(expected, cancellation_reason::deadline_exceeded,
            std::memory_order_acq_rel))
        return;
    if (cancelled_.exchange(true, std::memory_order_acq_rel))
        return;
    (void)dispatch_callback();
}

auto cancel_token::is_cancelled() const noexcept -> bool
{
    return cancelled_.load(std::memory_order_acquire);
}

auto cancel_token::reason() const noexcept -> cancellation_reason
{
    return reason_.load(std::memory_order_acquire);
}

void cancel_token::reset() noexcept
{
    concurrent_containers::exclusive_latch_guard lock{callback_latch_};
    callback_mode_ = false;
    callback_operation_ = nullptr;
    callback_notify_ = nullptr;
    cancelled_.store(false, std::memory_order_relaxed);
    reason_.store(cancellation_reason::none, std::memory_order_relaxed);
    pending_.store(false, std::memory_order_relaxed);
    cancel_fn_ = nullptr;
    ctx_ = nullptr;
    io_handle_ = nullptr;
    overlapped_ = nullptr;
    fd_ = -1;
    filter_ = 0;
    coroutine_ = {};
}

auto cancel_token::register_callback(void* operation, void (*notify)(void*) noexcept) noexcept -> bool
{
    concurrent_containers::exclusive_latch_guard lock{callback_latch_};
    if (is_cancelled())
        return false;
    callback_mode_ = true;
    callback_operation_ = operation;
    callback_notify_ = notify;
    pending_.store(true, std::memory_order_release);
    return true;
}

auto cancel_token::complete_callback(void* operation) noexcept -> bool
{
    concurrent_containers::exclusive_latch_guard lock{callback_latch_};
    if (callback_operation_ != operation || !callback_notify_)
        return false;
    callback_operation_ = nullptr;
    callback_notify_ = nullptr;
    callback_mode_ = false;
    pending_.store(false, std::memory_order_release);
    return true;
}

void cancel_token::finish_callback(void* operation) noexcept
{
    concurrent_containers::exclusive_latch_guard lock{callback_latch_};
    if (callback_operation_ != operation)
        return;
    callback_operation_ = nullptr;
    callback_notify_ = nullptr;
    callback_mode_ = false;
    pending_.store(false, std::memory_order_release);
}

auto cancel_token::dispatch_callback() noexcept -> bool
{
    void (*legacy_notify)(cancel_token&) noexcept = nullptr;
    {
        concurrent_containers::exclusive_latch_guard lock{callback_latch_};
        if (callback_mode_)
        {
            if (callback_operation_ && callback_notify_)
            {
                auto* operation = callback_operation_;
                const auto notify = std::exchange(callback_notify_, nullptr);
                notify(operation);
            }
            return true;
        }
        if (pending_.load(std::memory_order_acquire))
            legacy_notify = cancel_fn_;
    }
    // Existing platform callbacks may resume a waiter inline and reset its token.
    if (legacy_notify)
        legacy_notify(*this);
    return false;
}
} // namespace cnetmod
