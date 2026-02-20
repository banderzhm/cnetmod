/**
 * @file semaphore.cppm
 * @brief 协程计数信号量 — 限制并发数，不阻塞线程
 *
 * 使用示例:
 *   import cnetmod.coro.semaphore;
 *
 *   async_semaphore sem(10);  // 最多 10 个并发
 *
 *   co_await sem.acquire();
 *   // ... 受限操作 (如数据库连接) ...
 *   sem.release();
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.semaphore;

import std;

namespace cnetmod {

// =============================================================================
// async_semaphore — 协程计数信号量
// =============================================================================

/// 不阻塞线程的协程信号量
/// count > 0 时 acquire 立即返回，count == 0 时挂起等待 release
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
    // acquire — 获取一个许可
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
            // 再次尝试（可能在 suspend 前有 release）
            if (sem_.count_ > 0) {
                --sem_.count_;
                h.resume();
                return;
            }
            // 加入等待队列（FIFO）
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
    // release — 释放一个许可
    // =========================================================================

    /// 释放一个许可，唤醒等待队列中的下一个协程
    void release() noexcept {
        std::coroutine_handle<> to_resume;
        {
            std::lock_guard lock(mtx_);
            if (head_) {
                // 有等待者：直接传递许可（不增加 count_）
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

    /// 释放 n 个许可
    void release(std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i)
            release();
    }

    // =========================================================================
    // try_acquire — 非阻塞尝试
    // =========================================================================

    [[nodiscard]] auto try_acquire() noexcept -> bool {
        std::lock_guard lock(mtx_);
        if (count_ > 0) {
            --count_;
            return true;
        }
        return false;
    }

    /// 当前可用许可数（仅供监控，非线程安全快照）
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
