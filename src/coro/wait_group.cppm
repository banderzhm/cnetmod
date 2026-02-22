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
            // Count reached zero, wake all waiters
            std::vector<std::coroutine_handle<>> to_resume;
            {
                std::lock_guard lock(mtx_);
                while (head_) {
                    auto* w = head_;
                    head_ = w->next;
                    to_resume.push_back(w->handle);
                }
                tail_ = nullptr;
            }
            for (auto h : to_resume)
                h.resume();
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

        void await_suspend(std::coroutine_handle<> h) noexcept {
            node_.handle = h;
            node_.next = nullptr;
            std::lock_guard lock(wg_.mtx_);
            // Check again (may have reached zero before suspend)
            if (wg_.count_.load(std::memory_order_acquire) <= 0) {
                h.resume();
                return;
            }
            // Add to wait queue
            if (!wg_.tail_) {
                wg_.head_ = wg_.tail_ = &node_;
            } else {
                wg_.tail_->next = &node_;
                wg_.tail_ = &node_;
            }
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
    std::mutex mtx_;  // Protects wait queue
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

} // namespace cnetmod
