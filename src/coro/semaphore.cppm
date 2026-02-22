/**
 * @file semaphore.cppm
 * @brief Coroutine counting semaphore — Limit concurrency, non-blocking
 *
 * Usage example:
 *   import cnetmod.coro.semaphore;
 *
 *   async_semaphore sem(10);  // Max 10 concurrent
 *
 *   co_await sem.acquire();
 *   // ... Limited operation (e.g., database connection) ...
 *   sem.release();
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.semaphore;

import std;

namespace cnetmod {

// =============================================================================
// async_semaphore — Coroutine counting semaphore
// =============================================================================

/// Non-blocking coroutine semaphore
/// acquire returns immediately when count > 0, suspends when count == 0 waiting for release
export class async_semaphore {
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
    explicit async_semaphore(std::size_t initial_count) noexcept
        : count_(initial_count) {}

    ~async_semaphore() = default;

    async_semaphore(const async_semaphore&) = delete;
    auto operator=(const async_semaphore&) -> async_semaphore& = delete;

    // =========================================================================
    // acquire — Acquire a permit
    // =========================================================================

    struct [[nodiscard]] acquire_awaitable {
        async_semaphore& sem_;
        waiter_node node_;

        explicit acquire_awaitable(async_semaphore& sem) noexcept : sem_(sem) {}

        auto await_ready() noexcept -> bool {
            // Lock-free fast path: CAS decrement
            auto cur = sem_.count_.load(std::memory_order_acquire);
            while (cur > 0) {
                if (sem_.count_.compare_exchange_weak(
                        cur, cur - 1,
                        std::memory_order_acquire,
                        std::memory_order_relaxed))
                    return true;
            }
            return false;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            node_.handle = h;
            node_.next = nullptr;
            auto_lock g(sem_.lock_);
            // Re-check: permit may have been released between ready() and suspend()
            auto cur = sem_.count_.load(std::memory_order_acquire);
            while (cur > 0) {
                if (sem_.count_.compare_exchange_weak(
                        cur, cur - 1,
                        std::memory_order_acquire,
                        std::memory_order_relaxed))
                    return h;  // auto_lock destructs first, then resume
            }
            // Add to wait queue (FIFO)
            if (!sem_.tail_) {
                sem_.head_ = sem_.tail_ = &node_;
            } else {
                sem_.tail_->next = &node_;
                sem_.tail_ = &node_;
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    auto acquire() noexcept -> acquire_awaitable {
        return acquire_awaitable{*this};
    }

    // =========================================================================
    // release — Release a permit
    // =========================================================================

    /// Release a permit, wake up next coroutine in wait queue
    void release() noexcept {
        std::coroutine_handle<> to_resume;
        {
            auto_lock g(lock_);
            if (head_) {
                // Has waiters: directly transfer permit (don't increment count_)
                auto* w = head_;
                head_ = w->next;
                if (!head_) tail_ = nullptr;
                to_resume = w->handle;
            } else {
                count_.fetch_add(1, std::memory_order_release);
            }
        }
        if (to_resume) to_resume.resume();
    }

    /// Release n permits
    void release(std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i)
            release();
    }

    // =========================================================================
    // try_acquire — Non-blocking attempt
    // =========================================================================

    [[nodiscard]] auto try_acquire() noexcept -> bool {
        auto cur = count_.load(std::memory_order_acquire);
        while (cur > 0) {
            if (count_.compare_exchange_weak(
                    cur, cur - 1,
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    /// Current available permits (for monitoring only, not thread-safe snapshot)
    [[nodiscard]] auto available() const noexcept -> std::size_t {
        return count_.load(std::memory_order_relaxed);
    }

private:
    mutable spinlock lock_;
    std::atomic<std::size_t> count_;
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

} // namespace cnetmod
