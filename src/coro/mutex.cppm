module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.mutex;

import std;

namespace cnetmod {

// =============================================================================
// async_mutex — 协程互斥锁
// =============================================================================

/// 不阻塞线程的协程互斥锁
/// 冲突时挂起协程而非阻塞线程
export class async_mutex {
    struct waiter_node {
        std::coroutine_handle<> handle{};
        waiter_node* next = nullptr;
    };

public:
    async_mutex() noexcept = default;
    ~async_mutex() = default;

    async_mutex(const async_mutex&) = delete;
    auto operator=(const async_mutex&) -> async_mutex& = delete;

    /// co_await mtx.lock() 获取锁
    struct [[nodiscard]] lock_awaitable {
        async_mutex& mtx_;
        waiter_node node_;

        explicit lock_awaitable(async_mutex& mtx) noexcept : mtx_(mtx) {}

        auto await_ready() noexcept -> bool {
            // 尝试无竞争获取
            bool expected = false;
            return mtx_.locked_.compare_exchange_strong(
                expected, true, std::memory_order_acquire);
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            node_.handle = h;
            node_.next = nullptr;
            std::lock_guard lock(mtx_.mtx_);
            // 再次尝试获取（可能在 suspend 前已释放）
            bool expected = false;
            if (mtx_.locked_.compare_exchange_strong(
                    expected, true, std::memory_order_acquire)) {
                // 获取成功，直接恢复
                h.resume();
                return;
            }
            // 加入等待队列
            if (!mtx_.tail_) {
                mtx_.head_ = mtx_.tail_ = &node_;
            } else {
                mtx_.tail_->next = &node_;
                mtx_.tail_ = &node_;
            }
        }

        void await_resume() noexcept {}
    };

    auto lock() noexcept -> lock_awaitable {
        return lock_awaitable{*this};
    }

    /// 释放锁，唤醒下一个等待者
    void unlock() noexcept {
        std::coroutine_handle<> to_resume;
        {
            std::lock_guard lock(mtx_);
            if (head_) {
                // 唤醒队首等待者（锁传递，不释放 locked_）
                auto* w = head_;
                head_ = w->next;
                if (!head_) tail_ = nullptr;
                to_resume = w->handle;
            } else {
                // 无等待者，释放锁
                locked_.store(false, std::memory_order_release);
            }
        }
        if (to_resume) to_resume.resume();
    }

    /// 尝试获取锁（非阻塞）
    [[nodiscard]] auto try_lock() noexcept -> bool {
        bool expected = false;
        return locked_.compare_exchange_strong(
            expected, true, std::memory_order_acquire);
    }

private:
    std::atomic<bool> locked_{false};
    std::mutex mtx_;  // 保护等待队列
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

// =============================================================================
// async_lock_guard — RAII 锁守卫
// =============================================================================

/// 配合 async_mutex 使用的 RAII 守卫
/// 用法:
///   co_await mtx.lock();
///   async_lock_guard guard(mtx, std::adopt_lock);
///   // ... critical section ...
///   // guard 析构时自动 unlock
export class async_lock_guard {
public:
    explicit async_lock_guard(async_mutex& mtx, std::adopt_lock_t) noexcept
        : mtx_(&mtx) {}

    ~async_lock_guard() {
        if (mtx_) mtx_->unlock();
    }

    async_lock_guard(const async_lock_guard&) = delete;
    auto operator=(const async_lock_guard&) -> async_lock_guard& = delete;

    async_lock_guard(async_lock_guard&& other) noexcept
        : mtx_(std::exchange(other.mtx_, nullptr)) {}
    auto operator=(async_lock_guard&& other) noexcept -> async_lock_guard& {
        if (this != &other) {
            if (mtx_) mtx_->unlock();
            mtx_ = std::exchange(other.mtx_, nullptr);
        }
        return *this;
    }

    void release() noexcept { mtx_ = nullptr; }

private:
    async_mutex* mtx_;
};

} // namespace cnetmod
