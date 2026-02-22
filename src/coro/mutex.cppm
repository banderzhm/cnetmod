module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.mutex;

import std;

namespace cnetmod {

// =============================================================================
// async_mutex — Coroutine mutex
// =============================================================================

/// Non-blocking coroutine mutex
/// Suspends coroutine instead of blocking thread when contention occurs
export class async_mutex {
    struct waiter_node {
        std::coroutine_handle<> handle{};
        waiter_node* next = nullptr;
    };

public:
    async_mutex() noexcept = default;
    ~async_mutex() = default;

    async_mutex(const async_mutex&) = delete;
    auto operator=(const async_mutex&) -> async_mutex& = delete;

    /// co_await mtx.lock() to acquire lock
    struct [[nodiscard]] lock_awaitable {
        async_mutex& mtx_;
        waiter_node node_;

        explicit lock_awaitable(async_mutex& mtx) noexcept : mtx_(mtx) {}

        auto await_ready() noexcept -> bool {
            // Try lock-free acquisition
            bool expected = false;
            return mtx_.locked_.compare_exchange_strong(
                expected, true, std::memory_order_acquire);
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            node_.handle = h;
            node_.next = nullptr;
            std::lock_guard lock(mtx_.mtx_);
            // Try again (may have been released before suspend)
            bool expected = false;
            if (mtx_.locked_.compare_exchange_strong(
                    expected, true, std::memory_order_acquire)) {
                // Acquired successfully, resume directly
                h.resume();
                return;
            }
            // Add to wait queue
            if (!mtx_.tail_) {
                mtx_.head_ = mtx_.tail_ = &node_;
            } else {
                mtx_.tail_->next = &node_;
                mtx_.tail_ = &node_;
            }
        }

        void await_resume() noexcept {}
    };

    auto lock() noexcept -> lock_awaitable {
        return lock_awaitable{*this};
    }

    /// Release lock, wake up next waiter
    void unlock() noexcept {
        std::coroutine_handle<> to_resume;
        {
            std::lock_guard lock(mtx_);
            if (head_) {
                // Wake up head waiter (lock transfer, don't release locked_)
                auto* w = head_;
                head_ = w->next;
                if (!head_) tail_ = nullptr;
                to_resume = w->handle;
            } else {
                // No waiters, release lock
                locked_.store(false, std::memory_order_release);
            }
        }
        if (to_resume) to_resume.resume();
    }

    /// Try to acquire lock (non-blocking)
    [[nodiscard]] auto try_lock() noexcept -> bool {
        bool expected = false;
        return locked_.compare_exchange_strong(
            expected, true, std::memory_order_acquire);
    }

private:
    std::atomic<bool> locked_{false};
    std::mutex mtx_;  // Protect wait queue
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

// =============================================================================
// async_lock_guard — RAII lock guard
// =============================================================================

/// RAII guard for use with async_mutex
/// Usage:
///   co_await mtx.lock();
///   async_lock_guard guard(mtx, std::adopt_lock);
///   // ... critical section ...
///   // guard destructor automatically unlocks
export class async_lock_guard {
public:
    explicit async_lock_guard(async_mutex& mtx, std::adopt_lock_t) noexcept
        : mtx_(&mtx) {}

    ~async_lock_guard() {
        if (mtx_) mtx_->unlock();
    }

    async_lock_guard(const async_lock_guard&) = delete;
    auto operator=(const async_lock_guard&) -> async_lock_guard& = delete;

    async_lock_guard(async_lock_guard&& other) noexcept
        : mtx_(std::exchange(other.mtx_, nullptr)) {}
    auto operator=(async_lock_guard&& other) noexcept -> async_lock_guard& {
        if (this != &other) {
            if (mtx_) mtx_->unlock();
            mtx_ = std::exchange(other.mtx_, nullptr);
        }
        return *this;
    }

    void release() noexcept { mtx_ = nullptr; }

private:
    async_mutex* mtx_;
};

} // namespace cnetmod
