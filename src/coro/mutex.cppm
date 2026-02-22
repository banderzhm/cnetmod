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
    // ---- Adaptive spinlock (atomic_flag + C++20 wait) ----

    class spinlock {
        std::atomic_flag flag_{};
    public:
        void lock() noexcept {
            if (!flag_.test_and_set(std::memory_order_acquire)) return;
            do {
                flag_.wait(true, std::memory_order_relaxed);
            } while (flag_.test_and_set(std::memory_order_acquire));
        }
        void unlock() noexcept {
            flag_.clear(std::memory_order_release);
            flag_.notify_one();
        }
    };

    struct auto_lock {
        spinlock& lk_;
        explicit auto_lock(spinlock& lk) noexcept : lk_(lk) { lk_.lock(); }
        ~auto_lock() { lk_.unlock(); }
        auto_lock(const auto_lock&) = delete;
        auto operator=(const auto_lock&) -> auto_lock& = delete;
    };

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

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            node_.handle = h;
            node_.next = nullptr;
            auto_lock g(mtx_.lock_);
            // Re-check: may have been released between ready() and suspend()
            bool expected = false;
            if (mtx_.locked_.compare_exchange_strong(
                    expected, true, std::memory_order_acquire)) {
                return h;  // auto_lock destructs first, then resume
            }
            // Add to wait queue
            if (!mtx_.tail_) {
                mtx_.head_ = mtx_.tail_ = &node_;
            } else {
                mtx_.tail_->next = &node_;
                mtx_.tail_ = &node_;
            }
            return std::noop_coroutine();
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
            auto_lock g(lock_);
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
    mutable spinlock lock_;
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
