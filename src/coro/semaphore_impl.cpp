module cnetmod.coro.semaphore;

import std;

namespace cnetmod
{
    void async_semaphore::spinlock::lock() noexcept
    {
        if (!flag_.test_and_set(std::memory_order_acquire)) return;
        do
        {
            flag_.wait(true, std::memory_order_relaxed);
        }
        while (flag_.test_and_set(std::memory_order_acquire));
    }

    void async_semaphore::spinlock::unlock() noexcept
    {
        flag_.clear(std::memory_order_release);
        flag_.notify_one();
    }

    async_semaphore::auto_lock::auto_lock(spinlock& lock) noexcept : lk_(lock)
    {
        lk_.lock();
    }

    async_semaphore::auto_lock::~auto_lock()
    {
        lk_.unlock();
    }

    async_semaphore::async_semaphore(std::size_t initial_count) noexcept
        : count_(initial_count)
    {
    }

    async_semaphore::acquire_awaitable::acquire_awaitable(async_semaphore& semaphore) noexcept
        : sem_(semaphore)
    {
    }

    auto async_semaphore::acquire_awaitable::await_ready() noexcept -> bool
    {
        auto count = sem_.count_.load(std::memory_order_acquire);
        while (count > 0)
        {
            if (sem_.count_.compare_exchange_weak(
                count, count - 1,
                std::memory_order_acquire,
                std::memory_order_relaxed))
            {
                return true;
            }
        }
        return false;
    }

    auto async_semaphore::acquire_awaitable::await_suspend(
        std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        node_.handle = handle;
        node_.next = nullptr;
        auto_lock guard(sem_.lock_);
        auto count = sem_.count_.load(std::memory_order_acquire);
        while (count > 0)
        {
            if (sem_.count_.compare_exchange_weak(
                count, count - 1,
                std::memory_order_acquire,
                std::memory_order_relaxed))
            {
                return handle;
            }
        }
        if (!sem_.tail_)
        {
            sem_.head_ = sem_.tail_ = &node_;
        }
        else
        {
            sem_.tail_->next = &node_;
            sem_.tail_ = &node_;
        }
        return std::noop_coroutine();
    }

    void async_semaphore::acquire_awaitable::await_resume() noexcept
    {
    }

    auto async_semaphore::acquire() noexcept -> acquire_awaitable
    {
        return acquire_awaitable{*this};
    }

    void async_semaphore::release() noexcept
    {
        std::coroutine_handle<> handle;
        {
            auto_lock guard(lock_);
            if (head_)
            {
                auto* waiter = head_;
                head_ = waiter->next;
                if (!head_) tail_ = nullptr;
                handle = waiter->handle;
            }
            else
            {
                count_.fetch_add(1, std::memory_order_release);
            }
        }
        if (handle) handle.resume();
    }

    void async_semaphore::release(std::size_t count) noexcept
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            release();
        }
    }

    auto async_semaphore::try_acquire() noexcept -> bool
    {
        auto count = count_.load(std::memory_order_acquire);
        while (count > 0)
        {
            if (count_.compare_exchange_weak(
                count, count - 1,
                std::memory_order_acquire,
                std::memory_order_relaxed))
            {
                return true;
            }
        }
        return false;
    }

    auto async_semaphore::available() const noexcept -> std::size_t
    {
        return count_.load(std::memory_order_relaxed);
    }
} // namespace cnetmod