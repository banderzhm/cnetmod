module cnetmod.coro.mutex;

import std;

namespace cnetmod
{
    void async_mutex::spinlock::lock() noexcept
    {
        if (!flag_.test_and_set(std::memory_order_acquire)) return;
        do
        {
            flag_.wait(true, std::memory_order_relaxed);
        }
        while (flag_.test_and_set(std::memory_order_acquire));
    }

    void async_mutex::spinlock::unlock() noexcept
    {
        flag_.clear(std::memory_order_release);
        flag_.notify_one();
    }

    async_mutex::auto_lock::auto_lock(spinlock& lock) noexcept : lk_(lock)
    {
        lk_.lock();
    }

    async_mutex::auto_lock::~auto_lock()
    {
        lk_.unlock();
    }

    async_mutex::lock_awaitable::lock_awaitable(async_mutex& mutex) noexcept
        : mtx_(mutex)
    {
    }

    auto async_mutex::lock_awaitable::await_ready() noexcept -> bool
    {
        bool expected = false;
        return mtx_.locked_.compare_exchange_strong(
            expected, true, std::memory_order_acquire);
    }

    auto async_mutex::lock_awaitable::await_suspend(
        std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        node_.handle = handle;
        node_.next = nullptr;
        auto_lock guard(mtx_.lock_);
        bool expected = false;
        if (mtx_.locked_.compare_exchange_strong(
            expected, true, std::memory_order_acquire))
        {
            return handle;
        }
        if (!mtx_.tail_)
        {
            mtx_.head_ = mtx_.tail_ = &node_;
        }
        else
        {
            mtx_.tail_->next = &node_;
            mtx_.tail_ = &node_;
        }
        return std::noop_coroutine();
    }

    void async_mutex::lock_awaitable::await_resume() noexcept
    {
    }

    auto async_mutex::lock() noexcept -> lock_awaitable
    {
        return lock_awaitable{*this};
    }

    void async_mutex::unlock() noexcept
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
                locked_.store(false, std::memory_order_release);
            }
        }
        if (handle) handle.resume();
    }

    auto async_mutex::try_lock() noexcept -> bool
    {
        bool expected = false;
        return locked_.compare_exchange_strong(
            expected, true, std::memory_order_acquire);
    }

    async_lock_guard::async_lock_guard(async_mutex& mutex, std::adopt_lock_t) noexcept
        : mtx_(&mutex)
    {
    }

    async_lock_guard::~async_lock_guard()
    {
        if (mtx_) mtx_->unlock();
    }

    async_lock_guard::async_lock_guard(async_lock_guard&& other) noexcept
        : mtx_(std::exchange(other.mtx_, nullptr))
    {
    }

    auto async_lock_guard::operator=(async_lock_guard&& other) noexcept
        -> async_lock_guard&
    {
        if (this != &other)
        {
            if (mtx_) mtx_->unlock();
            mtx_ = std::exchange(other.mtx_, nullptr);
        }
        return *this;
    }

    void async_lock_guard::release() noexcept
    {
        mtx_ = nullptr;
    }
} // namespace cnetmod