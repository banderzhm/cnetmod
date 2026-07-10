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
        void lock() noexcept;
        void unlock() noexcept;
    };

    struct auto_lock {
        spinlock& lk_;
        explicit auto_lock(spinlock& lk) noexcept;
        ~auto_lock();
        auto_lock(const auto_lock&) = delete;
        auto operator=(const auto_lock&) -> auto_lock& = delete;
    };

    struct waiter_node {
        std::coroutine_handle<> handle{};
        waiter_node* next = nullptr;
    };

public:
    explicit async_semaphore(std::size_t initial_count) noexcept;

    ~async_semaphore() = default;

    async_semaphore(const async_semaphore&) = delete;
    auto operator=(const async_semaphore&) -> async_semaphore& = delete;

    // =========================================================================
    // acquire — Acquire a permit
    // =========================================================================

    struct [[nodiscard]] acquire_awaitable {
        async_semaphore& sem_;
        waiter_node node_;

        explicit acquire_awaitable(async_semaphore& sem) noexcept;

        auto await_ready() noexcept -> bool;

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>;

        void await_resume() noexcept;
    };

    auto acquire() noexcept -> acquire_awaitable;

    // =========================================================================
    // release — Release a permit
    // =========================================================================

    /// Release a permit, wake up next coroutine in wait queue
    void release() noexcept;

    /// Release n permits
    void release(std::size_t n) noexcept;

    // =========================================================================
    // try_acquire — Non-blocking attempt
    // =========================================================================

    [[nodiscard]] auto try_acquire() noexcept -> bool;

    /// Current available permits (for monitoring only, not thread-safe snapshot)
    [[nodiscard]] auto available() const noexcept -> std::size_t;

private:
    mutable spinlock lock_;
    std::atomic<std::size_t> count_;
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

} // namespace cnetmod
