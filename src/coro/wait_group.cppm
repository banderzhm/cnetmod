/**
 * @file wait_group.cppm
 * @brief Coroutine Wait Group — Wait for a group of coroutines to complete (similar to Go sync.WaitGroup)
 *
 * Usage Example:
 *   import cnetmod.coro.wait_group;
 *
 *   async_wait_group wg;
 *   wg.add(3);
 *
 *   for (int i = 0; i < 3; ++i)
 *       spawn(ctx, worker(wg));  // worker calls wg.done() when complete
 *
 *   co_await wg.wait();  // Resumes after all workers complete
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.wait_group;

import std;

namespace cnetmod {

// =============================================================================
// async_wait_group — Coroutine Wait Group
// =============================================================================

/// Synchronization primitive for waiting on a group of coroutines to complete
/// - add(n): Increase pending completion count
/// - done(): Decrease count, wakes all wait() waiters when reaching zero
/// - co_await wait(): Resumes when count reaches zero (returns immediately if already zero)
export class async_wait_group {
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
    async_wait_group() noexcept = default;
    ~async_wait_group() = default;

    async_wait_group(const async_wait_group&) = delete;
    auto operator=(const async_wait_group&) -> async_wait_group& = delete;

    /// Increase pending task count
    void add(int n = 1) noexcept {
        count_.fetch_add(n, std::memory_order_relaxed);
    }

    /// Mark one task complete, wakes all wait() waiters when reaching zero
    void done() noexcept {
        if (count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Count reached zero, wake all waiters (steal list, walk outside lock)
            waiter_node* waiters = nullptr;
            {
                auto_lock g(lock_);
                waiters = head_;
                head_ = tail_ = nullptr;
            }
            for (auto* n = waiters; n;) {
                auto* nx = n->next;
                n->handle.resume();
                n = nx;
            }
        }
    }

    // =========================================================================
    // wait — Wait for count to reach zero
    // =========================================================================

    struct [[nodiscard]] wait_awaitable {
        async_wait_group& wg_;
        waiter_node node_;

        explicit wait_awaitable(async_wait_group& wg) noexcept : wg_(wg) {}

        auto await_ready() noexcept -> bool {
            return wg_.count_.load(std::memory_order_acquire) <= 0;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            node_.handle = h;
            node_.next = nullptr;
            auto_lock g(wg_.lock_);
            // Re-check: may have reached zero between ready() and suspend()
            if (wg_.count_.load(std::memory_order_acquire) <= 0) {
                return h;  // auto_lock destructs first, then resume
            }
            // Add to wait queue
            if (!wg_.tail_) {
                wg_.head_ = wg_.tail_ = &node_;
            } else {
                wg_.tail_->next = &node_;
                wg_.tail_ = &node_;
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    auto wait() noexcept -> wait_awaitable {
        return wait_awaitable{*this};
    }

    /// Current remaining count (for monitoring only)
    [[nodiscard]] auto count() const noexcept -> int {
        return count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> count_{0};
    mutable spinlock lock_;
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

} // namespace cnetmod
