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
            std::lock_guard lock(rw_.mtx_);
            // Can acquire read lock: no writer holding, no writer waiting
            if (rw_.state_ >= 0 && !rw_.write_head_) {
                ++rw_.state_;
                return true;
            }
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            node_.handle = h;
            node_.next = nullptr;
            std::lock_guard lock(rw_.mtx_);
            // Try again
            if (rw_.state_ >= 0 && !rw_.write_head_) {
                ++rw_.state_;
                h.resume();
                return;
            }
            // Add to read wait queue
            if (!rw_.read_tail_) {
                rw_.read_head_ = rw_.read_tail_ = &node_;
            } else {
                rw_.read_tail_->next = &node_;
                rw_.read_tail_ = &node_;
            }
        }

        void await_resume() noexcept {}
    };

    auto lock_shared() noexcept -> lock_shared_awaitable {
        return lock_shared_awaitable{*this};
    }

    /// Release read lock
    void unlock_shared() noexcept {
        std::coroutine_handle<> to_resume;
        std::vector<std::coroutine_handle<>> readers_to_resume;
        {
            std::lock_guard lock(mtx_);
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
            std::lock_guard lock(rw_.mtx_);
            if (rw_.state_ == 0) {
                rw_.state_ = -1;
                return true;
            }
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            node_.handle = h;
            node_.next = nullptr;
            std::lock_guard lock(rw_.mtx_);
            // Try again
            if (rw_.state_ == 0) {
                rw_.state_ = -1;
                h.resume();
                return;
            }
            // Add to write wait queue
            if (!rw_.write_tail_) {
                rw_.write_head_ = rw_.write_tail_ = &node_;
            } else {
                rw_.write_tail_->next = &node_;
                rw_.write_tail_ = &node_;
            }
        }

        void await_resume() noexcept {}
    };

    auto lock() noexcept -> lock_awaitable {
        return lock_awaitable{*this};
    }

    /// Non-coroutine context synchronous try to acquire write lock (non-blocking)
    [[nodiscard]] auto try_lock() noexcept -> bool {
        std::lock_guard lk(mtx_);
        if (state_ == 0) {
            state_ = -1;
            return true;
        }
        return false;
    }

    /// Release write lock
    void unlock() noexcept {
        std::coroutine_handle<> writer_to_resume;
        std::vector<std::coroutine_handle<>> readers_to_resume;
        {
            std::lock_guard lock(mtx_);

            // Prioritize waking next writer (lock handoff)
            if (write_head_) {
                // state_ remains -1 (write lock directly transferred)
                auto* w = write_head_;
                write_head_ = w->next;
                if (!write_head_) write_tail_ = nullptr;
                writer_to_resume = w->handle;
            } else if (read_head_) {
                // No writer waiting → wake all readers
                int count = 0;
                while (read_head_) {
                    auto* r = read_head_;
                    read_head_ = r->next;
                    readers_to_resume.push_back(r->handle);
                    ++count;
                }
                read_tail_ = nullptr;
                state_ = count;
            } else {
                state_ = 0;
            }
        }
        // Resume writer first, or resume all readers
        if (writer_to_resume) {
            writer_to_resume.resume();
        } else {
            for (auto h : readers_to_resume)
                h.resume();
        }
    }

private:
    std::mutex mtx_;                     // Protect internal state and wait queues
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
