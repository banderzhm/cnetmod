module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.mutex;

import std;
import cnetmod.io.io_context;

namespace cnetmod {

// =============================================================================
// async_mutex — Coroutine mutex
// =============================================================================

/// Non-blocking coroutine mutex
/// Suspends coroutine instead of blocking thread when contention occurs
export class async_mutex
{
    // ---- Adaptive spinlock (atomic_flag + C++20 wait) ----

    class spinlock
    {
        // 0 = free, 1 = held, 2 = held with a sleeping contender. Unlock
        // only enters the platform wait table when a contender slept.
        std::atomic<std::uint32_t> state_{};

    public:
        void lock() noexcept;
        void unlock() noexcept;
    };

    struct auto_lock
    {
        spinlock& lk_;
        explicit auto_lock(spinlock& lk) noexcept;
        ~auto_lock();
        auto_lock(const auto_lock&) = delete;
        auto operator=(const auto_lock&) -> auto_lock& = delete;
    };

    struct waiter_node
    {
        std::coroutine_handle<> handle{};
        io_context* event_loop = nullptr;
        waiter_node* next = nullptr;
    };

public:
    async_mutex() noexcept = default;
    ~async_mutex() = default;

    async_mutex(const async_mutex&) = delete;
    auto operator=(const async_mutex&) -> async_mutex& = delete;

    /// co_await mtx.lock() to acquire lock
    struct [[nodiscard]] lock_awaitable
    {
        async_mutex& mtx_;
        waiter_node node_;

        inline explicit lock_awaitable(async_mutex& mtx) noexcept : mtx_(mtx) {}

        inline auto await_ready() noexcept -> bool { return mtx_.try_lock(); }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>;

        inline void await_resume() noexcept {}
    };

    inline auto lock() noexcept -> lock_awaitable { return lock_awaitable{*this}; }

    /// Release lock, wake up next waiter. Uncontended, this is one CAS.
    inline void unlock() noexcept
    {
        std::uint32_t expected = locked;
        if (!state_.compare_exchange_strong(expected, unlocked,
                std::memory_order_release, std::memory_order_relaxed)) [[unlikely]]
            unlock_contended();
    }

    /// Try to acquire lock (non-blocking). One CAS.
    [[nodiscard]] inline auto try_lock() noexcept -> bool
    {
        std::uint32_t expected = unlocked;
        return state_.compare_exchange_strong(expected, locked,
            std::memory_order_acquire, std::memory_order_relaxed);
    }

private:
    // unlocked; locked with no queued waiter; locked and a waiter may be
    // queued. Only `contended` sends unlock to the queue, so the common
    // uncontended lock/unlock pair is two CAS operations and no queue lock.
    static constexpr std::uint32_t unlocked = 0U;
    static constexpr std::uint32_t locked = 1U;
    static constexpr std::uint32_t contended = 2U;

    void unlock_contended() noexcept;

    std::atomic<std::uint32_t> state_{unlocked};
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
export class async_lock_guard
{
public:
    inline explicit async_lock_guard(async_mutex& mtx, std::adopt_lock_t) noexcept
        : mtx_(&mtx)
    {
    }

    inline ~async_lock_guard()
    {
        if (mtx_)
            mtx_->unlock();
    }

    async_lock_guard(const async_lock_guard&) = delete;
    auto operator=(const async_lock_guard&) -> async_lock_guard& = delete;

    async_lock_guard(async_lock_guard&& other) noexcept;
    auto operator=(async_lock_guard&& other) noexcept -> async_lock_guard&;

    inline void release() noexcept { mtx_ = nullptr; }

private:
    async_mutex* mtx_;
};

} // namespace cnetmod
