/**
 * @file shared_mutex.cppm
 * @brief 协程读写锁 — 不阻塞线程，支持并发读 + 独占写
 *
 * 使用示例:
 *   import cnetmod.coro.shared_mutex;
 *
 *   async_shared_mutex rw;
 *
 *   // 读锁 (多个协程可同时持有)
 *   co_await rw.lock_shared();
 *   async_shared_lock_guard rg(rw, std::adopt_lock);
 *   // ... read ...
 *
 *   // 写锁 (独占)
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
// async_shared_mutex — 协程读写锁
// =============================================================================

/// 不阻塞线程的协程读写锁
/// - lock_shared / unlock_shared: 读锁 (共享, 多个读者并发)
/// - lock / unlock: 写锁 (独占)
/// - 写者优先: 有写等待时新读者排队，防止写者饿死
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
    // 共享读锁
    // =========================================================================

    struct [[nodiscard]] lock_shared_awaitable {
        async_shared_mutex& rw_;
        waiter_node node_;

        explicit lock_shared_awaitable(async_shared_mutex& rw) noexcept : rw_(rw) {}

        auto await_ready() noexcept -> bool {
            std::lock_guard lock(rw_.mtx_);
            // 可以获取读锁: 没有写者持有、没有写者等待
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
            // 再次尝试
            if (rw_.state_ >= 0 && !rw_.write_head_) {
                ++rw_.state_;
                h.resume();
                return;
            }
            // 加入读等待队列
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

    /// 释放读锁
    void unlock_shared() noexcept {
        std::coroutine_handle<> to_resume;
        std::vector<std::coroutine_handle<>> readers_to_resume;
        {
            std::lock_guard lock(mtx_);
            --state_;

            // 最后一个读者释放 → 尝试唤醒写者
            if (state_ == 0 && write_head_) {
                state_ = -1;  // 转为写锁
                auto* w = write_head_;
                write_head_ = w->next;
                if (!write_head_) write_tail_ = nullptr;
                to_resume = w->handle;
            }
        }
        if (to_resume) to_resume.resume();
    }

    // =========================================================================
    // 独占写锁
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
            // 再次尝试
            if (rw_.state_ == 0) {
                rw_.state_ = -1;
                h.resume();
                return;
            }
            // 加入写等待队列
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

    /// 非协程上下文同步尝试获取写锁（非阻塞）
    [[nodiscard]] auto try_lock() noexcept -> bool {
        std::lock_guard lk(mtx_);
        if (state_ == 0) {
            state_ = -1;
            return true;
        }
        return false;
    }

    /// 释放写锁
    void unlock() noexcept {
        std::coroutine_handle<> writer_to_resume;
        std::vector<std::coroutine_handle<>> readers_to_resume;
        {
            std::lock_guard lock(mtx_);

            // 优先唤醒下一个写者 (锁传递)
            if (write_head_) {
                // state_ 保持 -1 (写锁直接移交)
                auto* w = write_head_;
                write_head_ = w->next;
                if (!write_head_) write_tail_ = nullptr;
                writer_to_resume = w->handle;
            } else if (read_head_) {
                // 没有写者等待 → 唤醒所有读者
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
        // 先 resume writer，或 resume 所有 readers
        if (writer_to_resume) {
            writer_to_resume.resume();
        } else {
            for (auto h : readers_to_resume)
                h.resume();
        }
    }

private:
    std::mutex mtx_;                     // 保护内部状态和等待队列
    int state_ = 0;                      // 0=free, >0=N readers, -1=writer

    waiter_node* write_head_ = nullptr;
    waiter_node* write_tail_ = nullptr;
    waiter_node* read_head_  = nullptr;
    waiter_node* read_tail_  = nullptr;
};

// =============================================================================
// async_shared_lock_guard — 读锁 RAII 守卫
// =============================================================================

/// 用法:
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
// async_unique_lock_guard — 写锁 RAII 守卫
// =============================================================================

/// 用法:
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
