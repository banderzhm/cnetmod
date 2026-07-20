/**
 * @file shared_mutex.cppm
 * @brief Coroutine read-write lock — Non-blocking, supports concurrent reads + exclusive writes
 *
 * Usage example:
 *   import cnetmod.coro.shared_mutex;
 *
 *   async_shared_mutex rw;
 *
 *   // Read lock (multiple coroutines can hold simultaneously)
 *   co_await rw.lock_shared();
 *   async_shared_lock_guard rg(rw, std::adopt_lock);
 *   // ... read ...
 *
 *   // Write lock (exclusive)
 *   co_await rw.lock();
 *   async_unique_lock_guard wg(rw, std::adopt_lock);
 *   // ... write ...
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.shared_mutex;

import std;

namespace cnetmod {

// =============================================================================
// async_shared_mutex — Coroutine read-write lock
// =============================================================================

/// Non-blocking coroutine read-write lock
/// - lock_shared / unlock_shared: Read lock (shared, multiple readers concurrent)
/// - lock / unlock: Write lock (exclusive)
/// - Writer priority: New readers queue when writers waiting, prevents writer starvation
export class async_shared_mutex {
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
    async_shared_mutex() noexcept = default;
    ~async_shared_mutex() = default;

    async_shared_mutex(const async_shared_mutex&) = delete;
    auto operator=(const async_shared_mutex&) -> async_shared_mutex& = delete;

    // =========================================================================
    // Shared read lock
    // =========================================================================

    struct [[nodiscard]] lock_shared_awaitable {
        async_shared_mutex& rw_;
        waiter_node node_;

        explicit lock_shared_awaitable(async_shared_mutex& rw) noexcept;

        auto await_ready() noexcept -> bool;

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>;

        void await_resume() noexcept;
    };

    auto lock_shared() noexcept -> lock_shared_awaitable;

    /// Release read lock
    void unlock_shared() noexcept;

    // =========================================================================
    // Exclusive write lock
    // =========================================================================

    struct [[nodiscard]] lock_awaitable {
        async_shared_mutex& rw_;
        waiter_node node_;

        explicit lock_awaitable(async_shared_mutex& rw) noexcept;

        auto await_ready() noexcept -> bool;

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>;

        void await_resume() noexcept;
    };

    auto lock() noexcept -> lock_awaitable;

    /// Non-coroutine context synchronous try to acquire write lock (non-blocking)
    [[nodiscard]] auto try_lock() noexcept -> bool;

    /// Release write lock
    void unlock() noexcept;

private:
    mutable spinlock lock_;
    int state_ = 0;                      // 0=free, >0=N readers, -1=writer

    waiter_node* write_head_ = nullptr;
    waiter_node* write_tail_ = nullptr;
    waiter_node* read_head_  = nullptr;
    waiter_node* read_tail_  = nullptr;
};

// =============================================================================
// async_shared_lock_guard — read lock RAII guard
// =============================================================================

/// Usage:
///   co_await rw.lock_shared();
///   async_shared_lock_guard guard(rw, std::adopt_lock);
export class async_shared_lock_guard {
public:
    explicit async_shared_lock_guard(async_shared_mutex& rw,
                                     std::adopt_lock_t) noexcept;

    ~async_shared_lock_guard();

    async_shared_lock_guard(const async_shared_lock_guard&) = delete;
    auto operator=(const async_shared_lock_guard&) -> async_shared_lock_guard& = delete;

    async_shared_lock_guard(async_shared_lock_guard&& o) noexcept;
    auto operator=(async_shared_lock_guard&& o) noexcept -> async_shared_lock_guard&;

    void release() noexcept;

private:
    async_shared_mutex* rw_;
};

// =============================================================================
// async_unique_lock_guard — write lock RAII guard
// =============================================================================

/// Usage:
///   co_await rw.lock();
///   async_unique_lock_guard guard(rw, std::adopt_lock);
export class async_unique_lock_guard {
public:
    explicit async_unique_lock_guard(async_shared_mutex& rw,
                                     std::adopt_lock_t) noexcept;

    ~async_unique_lock_guard();

    async_unique_lock_guard(const async_unique_lock_guard&) = delete;
    auto operator=(const async_unique_lock_guard&) -> async_unique_lock_guard& = delete;

    async_unique_lock_guard(async_unique_lock_guard&& o) noexcept;
    auto operator=(async_unique_lock_guard&& o) noexcept -> async_unique_lock_guard&;

    void release() noexcept;

private:
    async_shared_mutex* rw_;
};

} // namespace cnetmod
