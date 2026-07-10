module cnetmod.coro.shared_mutex;

import std;

namespace cnetmod
{
    void async_shared_mutex::spinlock::lock() noexcept
    {
        if (!flag_.test_and_set(std::memory_order_acquire)) return;
        do
        {
            flag_.wait(true, std::memory_order_relaxed);
        }
        while (flag_.test_and_set(std::memory_order_acquire));
    }

    void async_shared_mutex::spinlock::unlock() noexcept
    {
        flag_.clear(std::memory_order_release);
        flag_.notify_one();
    }

    async_shared_mutex::auto_lock::auto_lock(spinlock& lock) noexcept : lk_(lock)
    {
        lk_.lock();
    }

    async_shared_mutex::auto_lock::~auto_lock()
    {
        lk_.unlock();
    }

    async_shared_mutex::lock_shared_awaitable::lock_shared_awaitable(
        async_shared_mutex& mutex) noexcept : rw_(mutex)
    {
    }

    auto async_shared_mutex::lock_shared_awaitable::await_ready() noexcept -> bool
    {
        auto_lock guard(rw_.lock_);
        if (rw_.state_ >= 0 && !rw_.write_head_)
        {
            ++rw_.state_;
            return true;
        }
        return false;
    }

    auto async_shared_mutex::lock_shared_awaitable::await_suspend(
        std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        node_.handle = handle;
        node_.next = nullptr;
        auto_lock guard(rw_.lock_);
        if (rw_.state_ >= 0 && !rw_.write_head_)
        {
            ++rw_.state_;
            return handle;
        }
        if (!rw_.read_tail_)
        {
            rw_.read_head_ = rw_.read_tail_ = &node_;
        }
        else
        {
            rw_.read_tail_->next = &node_;
            rw_.read_tail_ = &node_;
        }
        return std::noop_coroutine();
    }

    void async_shared_mutex::lock_shared_awaitable::await_resume() noexcept
    {
    }

    auto async_shared_mutex::lock_shared() noexcept -> lock_shared_awaitable
    {
        return lock_shared_awaitable{*this};
    }

    void async_shared_mutex::unlock_shared() noexcept
    {
        std::coroutine_handle<> handle;
        {
            auto_lock guard(lock_);
            --state_;
            if (state_ == 0 && write_head_)
            {
                state_ = -1;
                auto* waiter = write_head_;
                write_head_ = waiter->next;
                if (!write_head_) write_tail_ = nullptr;
                handle = waiter->handle;
            }
        }
        if (handle) handle.resume();
    }

    async_shared_mutex::lock_awaitable::lock_awaitable(async_shared_mutex& mutex) noexcept
        : rw_(mutex)
    {
    }

    auto async_shared_mutex::lock_awaitable::await_ready() noexcept -> bool
    {
        auto_lock guard(rw_.lock_);
        if (rw_.state_ == 0)
        {
            rw_.state_ = -1;
            return true;
        }
        return false;
    }

    auto async_shared_mutex::lock_awaitable::await_suspend(
        std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        node_.handle = handle;
        node_.next = nullptr;
        auto_lock guard(rw_.lock_);
        if (rw_.state_ == 0)
        {
            rw_.state_ = -1;
            return handle;
        }
        if (!rw_.write_tail_)
        {
            rw_.write_head_ = rw_.write_tail_ = &node_;
        }
        else
        {
            rw_.write_tail_->next = &node_;
            rw_.write_tail_ = &node_;
        }
        return std::noop_coroutine();
    }

    void async_shared_mutex::lock_awaitable::await_resume() noexcept
    {
    }

    auto async_shared_mutex::lock() noexcept -> lock_awaitable
    {
        return lock_awaitable{*this};
    }

    auto async_shared_mutex::try_lock() noexcept -> bool
    {
        auto_lock guard(lock_);
        if (state_ != 0) return false;
        state_ = -1;
        return true;
    }

    void async_shared_mutex::unlock() noexcept
    {
        std::coroutine_handle<> writer;
        waiter_node* readers = nullptr;
        {
            auto_lock guard(lock_);
            if (write_head_)
            {
                auto* waiter = write_head_;
                write_head_ = waiter->next;
                if (!write_head_) write_tail_ = nullptr;
                writer = waiter->handle;
            }
            else if (read_head_)
            {
                int count = 0;
                for (auto* node = read_head_; node; node = node->next) ++count;
                state_ = count;
                readers = read_head_;
                read_head_ = read_tail_ = nullptr;
            }
            else
            {
                state_ = 0;
            }
        }
        if (writer)
        {
            writer.resume();
            return;
        }
        for (auto* node = readers; node;)
        {
            auto* next = node->next;
            node->handle.resume();
            node = next;
        }
    }

    async_shared_lock_guard::async_shared_lock_guard(
        async_shared_mutex& mutex, std::adopt_lock_t) noexcept : rw_(&mutex)
    {
    }

    async_shared_lock_guard::~async_shared_lock_guard()
    {
        if (rw_) rw_->unlock_shared();
    }

    async_shared_lock_guard::async_shared_lock_guard(async_shared_lock_guard&& other) noexcept
        : rw_(std::exchange(other.rw_, nullptr))
    {
    }

    auto async_shared_lock_guard::operator=(async_shared_lock_guard&& other) noexcept
        -> async_shared_lock_guard&
    {
        if (this != &other)
        {
            if (rw_) rw_->unlock_shared();
            rw_ = std::exchange(other.rw_, nullptr);
        }
        return *this;
    }

    void async_shared_lock_guard::release() noexcept
    {
        rw_ = nullptr;
    }

    async_unique_lock_guard::async_unique_lock_guard(
        async_shared_mutex& mutex, std::adopt_lock_t) noexcept : rw_(&mutex)
    {
    }

    async_unique_lock_guard::~async_unique_lock_guard()
    {
        if (rw_) rw_->unlock();
    }

    async_unique_lock_guard::async_unique_lock_guard(async_unique_lock_guard&& other) noexcept
        : rw_(std::exchange(other.rw_, nullptr))
    {
    }

    auto async_unique_lock_guard::operator=(async_unique_lock_guard&& other) noexcept
        -> async_unique_lock_guard&
    {
        if (this != &other)
        {
            if (rw_) rw_->unlock();
            rw_ = std::exchange(other.rw_, nullptr);
        }
        return *this;
    }

    void async_unique_lock_guard::release() noexcept
    {
        rw_ = nullptr;
    }
} // namespace cnetmod