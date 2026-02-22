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
            std::lock_guard lock(sem_.mtx_);
            if (sem_.count_ > 0) {
                --sem_.count_;
                return true;
            }
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            node_.handle = h;
            node_.next = nullptr;
            std::lock_guard lock(sem_.mtx_);
            // Try again (may have release before suspend)
            if (sem_.count_ > 0) {
                --sem_.count_;
                h.resume();
                return;
            }
            // Add to wait queue (FIFO)
            if (!sem_.tail_) {
                sem_.head_ = sem_.tail_ = &node_;
            } else {
                sem_.tail_->next = &node_;
                sem_.tail_ = &node_;
            }
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
            std::lock_guard lock(mtx_);
            if (head_) {
                // Has waiters: directly transfer permit (don't increment count_)
                auto* w = head_;
                head_ = w->next;
                if (!head_) tail_ = nullptr;
                to_resume = w->handle;
            } else {
                ++count_;
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
        std::lock_guard lock(mtx_);
        if (count_ > 0) {
            --count_;
            return true;
        }
        return false;
    }

    /// Current available permits (for monitoring only, not thread-safe snapshot)
    [[nodiscard]] auto available() const noexcept -> std::size_t {
        std::lock_guard lock(mtx_);
        return count_;
    }

private:
    mutable std::mutex mtx_;
    std::size_t count_;
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

} // namespace cnetmod
