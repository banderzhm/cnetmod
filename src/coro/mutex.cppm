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
    async_mutex() noexcept = default;
    ~async_mutex() = default;

    async_mutex(const async_mutex&) = delete;
    auto operator=(const async_mutex&) -> async_mutex& = delete;

    /// co_await mtx.lock() to acquire lock
    struct [[nodiscard]] lock_awaitable {
        async_mutex& mtx_;
        waiter_node node_;

        explicit lock_awaitable(async_mutex& mtx) noexcept;

        auto await_ready() noexcept -> bool;

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>;

        void await_resume() noexcept;
    };

    auto lock() noexcept -> lock_awaitable;

    /// Release lock, wake up next waiter
    void unlock() noexcept;

    /// Try to acquire lock (non-blocking)
    [[nodiscard]] auto try_lock() noexcept -> bool;

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
    explicit async_lock_guard(async_mutex& mtx, std::adopt_lock_t) noexcept;

    ~async_lock_guard();

    async_lock_guard(const async_lock_guard&) = delete;
    auto operator=(const async_lock_guard&) -> async_lock_guard& = delete;

    async_lock_guard(async_lock_guard&& other) noexcept;
    auto operator=(async_lock_guard&& other) noexcept -> async_lock_guard&;

    void release() noexcept;

private:
    async_mutex* mtx_;
};

} // namespace cnetmod
