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

        explicit lock_shared_awaitable(async_shared_mutex& rw) noexcept : rw_(rw) {}

        auto await_ready() noexcept -> bool {
            auto_lock g(rw_.lock_);
            // Can acquire read lock: no writer holding, no writer waiting
            if (rw_.state_ >= 0 && !rw_.write_head_) {
                ++rw_.state_;
                return true;
            }
            return false;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            node_.handle = h;
            node_.next = nullptr;
            auto_lock g(rw_.lock_);
            // Re-check: state may have changed between ready() and suspend()
            if (rw_.state_ >= 0 && !rw_.write_head_) {
                ++rw_.state_;
                return h;  // auto_lock destructs first, then resume
            }
            // Add to read wait queue
            if (!rw_.read_tail_) {
                rw_.read_head_ = rw_.read_tail_ = &node_;
            } else {
                rw_.read_tail_->next = &node_;
                rw_.read_tail_ = &node_;
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    auto lock_shared() noexcept -> lock_shared_awaitable {
        return lock_shared_awaitable{*this};
    }

    /// Release read lock
    void unlock_shared() noexcept {
        std::coroutine_handle<> to_resume;
        {
            auto_lock g(lock_);
            --state_;

            // Last reader releases → try to wake writer
            if (state_ == 0 && write_head_) {
                state_ = -1;  // Convert to write lock
                auto* w = write_head_;
                write_head_ = w->next;
                if (!write_head_) write_tail_ = nullptr;
                to_resume = w->handle;
            }
        }
        if (to_resume) to_resume.resume();
    }

    // =========================================================================
    // Exclusive write lock
    // =========================================================================

    struct [[nodiscard]] lock_awaitable {
        async_shared_mutex& rw_;
        waiter_node node_;

        explicit lock_awaitable(async_shared_mutex& rw) noexcept : rw_(rw) {}

        auto await_ready() noexcept -> bool {
            auto_lock g(rw_.lock_);
            if (rw_.state_ == 0) {
                rw_.state_ = -1;
                return true;
            }
            return false;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            node_.handle = h;
            node_.next = nullptr;
            auto_lock g(rw_.lock_);
            // Re-check: state may have changed between ready() and suspend()
            if (rw_.state_ == 0) {
                rw_.state_ = -1;
                return h;  // auto_lock destructs first, then resume
            }
            // Add to write wait queue
            if (!rw_.write_tail_) {
                rw_.write_head_ = rw_.write_tail_ = &node_;
            } else {
                rw_.write_tail_->next = &node_;
                rw_.write_tail_ = &node_;
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    auto lock() noexcept -> lock_awaitable {
        return lock_awaitable{*this};
    }

    /// Non-coroutine context synchronous try to acquire write lock (non-blocking)
    [[nodiscard]] auto try_lock() noexcept -> bool {
        auto_lock g(lock_);
        if (state_ == 0) {
            state_ = -1;
            return true;
        }
        return false;
    }

    /// Release write lock
    void unlock() noexcept {
        std::coroutine_handle<> writer_to_resume;
        waiter_node* readers = nullptr;
        {
            auto_lock g(lock_);

            // Prioritize waking next writer (lock handoff)
            if (write_head_) {
                // state_ remains -1 (write lock directly transferred)
                auto* w = write_head_;
                write_head_ = w->next;
                if (!write_head_) write_tail_ = nullptr;
                writer_to_resume = w->handle;
            } else if (read_head_) {
                // No writer waiting → wake all readers (steal list, walk outside lock)
                int count = 0;
                for (auto* n = read_head_; n; n = n->next) ++count;
                state_ = count;
                readers = read_head_;
                read_head_ = read_tail_ = nullptr;
            } else {
                state_ = 0;
            }
        }
        // Resume writer first, or resume all readers
        if (writer_to_resume) {
            writer_to_resume.resume();
        } else {
            for (auto* n = readers; n;) {
                auto* nx = n->next;
                n->handle.resume();
                n = nx;
            }
        }
    }

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
                                     std::adopt_lock_t) noexcept
        : rw_(&rw) {}

    ~async_shared_lock_guard() {
        if (rw_) rw_->unlock_shared();
    }

    async_shared_lock_guard(const async_shared_lock_guard&) = delete;
    auto operator=(const async_shared_lock_guard&) -> async_shared_lock_guard& = delete;

    async_shared_lock_guard(async_shared_lock_guard&& o) noexcept
        : rw_(std::exchange(o.rw_, nullptr)) {}
    auto operator=(async_shared_lock_guard&& o) noexcept -> async_shared_lock_guard& {
        if (this != &o) {
            if (rw_) rw_->unlock_shared();
            rw_ = std::exchange(o.rw_, nullptr);
        }
        return *this;
    }

    void release() noexcept { rw_ = nullptr; }

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
                                     std::adopt_lock_t) noexcept
        : rw_(&rw) {}

    ~async_unique_lock_guard() {
        if (rw_) rw_->unlock();
    }

    async_unique_lock_guard(const async_unique_lock_guard&) = delete;
    auto operator=(const async_unique_lock_guard&) -> async_unique_lock_guard& = delete;

    async_unique_lock_guard(async_unique_lock_guard&& o) noexcept
        : rw_(std::exchange(o.rw_, nullptr)) {}
    auto operator=(async_unique_lock_guard&& o) noexcept -> async_unique_lock_guard& {
        if (this != &o) {
            if (rw_) rw_->unlock();
            rw_ = std::exchange(o.rw_, nullptr);
        }
        return *this;
    }

    void release() noexcept { rw_ = nullptr; }

private:
    async_shared_mutex* rw_;
};

} // namespace cnetmod
