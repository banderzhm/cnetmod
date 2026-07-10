module cnetmod.coro.wait_group;

import std;

namespace cnetmod
{
    void async_wait_group::spinlock::lock() noexcept
    {
        if (!flag_.test_and_set(std::memory_order_acquire)) return;
        do
        {
            flag_.wait(true, std::memory_order_relaxed);
        }
        while (flag_.test_and_set(std::memory_order_acquire));
    }

    void async_wait_group::spinlock::unlock() noexcept
    {
        flag_.clear(std::memory_order_release);
        flag_.notify_one();
    }

    async_wait_group::auto_lock::auto_lock(spinlock& lock) noexcept : lk_(lock)
    {
        lk_.lock();
    }

    async_wait_group::auto_lock::~auto_lock()
    {
        lk_.unlock();
    }

    void async_wait_group::add(int count) noexcept
    {
        count_.fetch_add(count, std::memory_order_relaxed);
    }

    void async_wait_group::done() noexcept
    {
        if (count_.fetch_sub(1, std::memory_order_acq_rel) != 1) return;

        waiter_node* waiters = nullptr;
        {
            auto_lock guard(lock_);
            waiters = head_;
            head_ = tail_ = nullptr;
        }
        for (auto* waiter = waiters; waiter;)
        {
            auto* next = waiter->next;
            waiter->handle.resume();
            waiter = next;
        }
    }

    async_wait_group::wait_awaitable::wait_awaitable(async_wait_group& group) noexcept
        : wg_(group)
    {
    }

    auto async_wait_group::wait_awaitable::await_ready() noexcept -> bool
    {
        return wg_.count_.load(std::memory_order_acquire) <= 0;
    }

    auto async_wait_group::wait_awaitable::await_suspend(
        std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        node_.handle = handle;
        node_.next = nullptr;
        auto_lock guard(wg_.lock_);
        if (wg_.count_.load(std::memory_order_acquire) <= 0)
        {
            return handle;
        }
        if (!wg_.tail_)
        {
            wg_.head_ = wg_.tail_ = &node_;
        }
        else
        {
            wg_.tail_->next = &node_;
            wg_.tail_ = &node_;
        }
        return std::noop_coroutine();
    }

    void async_wait_group::wait_awaitable::await_resume() noexcept
    {
    }

    auto async_wait_group::wait() noexcept -> wait_awaitable
    {
        return wait_awaitable{*this};
    }

    auto async_wait_group::count() const noexcept -> int
    {
        return count_.load(std::memory_order_relaxed);
    }
} // namespace cnetmod