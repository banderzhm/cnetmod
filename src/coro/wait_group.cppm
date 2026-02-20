/**
 * @file wait_group.cppm
 * @brief 协程等待组 — 等待一组协程全部完成（类似 Go sync.WaitGroup）
 *
 * 使用示例:
 *   import cnetmod.coro.wait_group;
 *
 *   async_wait_group wg;
 *   wg.add(3);
 *
 *   for (int i = 0; i < 3; ++i)
 *       spawn(ctx, worker(wg));  // worker 完成时调用 wg.done()
 *
 *   co_await wg.wait();  // 所有 worker 完成后恢复
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.wait_group;

import std;

namespace cnetmod {

// =============================================================================
// async_wait_group — 协程等待组
// =============================================================================

/// 等待一组协程完成的同步原语
/// - add(n): 增加待完成计数
/// - done(): 减少计数，归零时唤醒所有 wait() 等待者
/// - co_await wait(): 计数归零时恢复（已归零则立即返回）
export class async_wait_group {
    struct waiter_node {
        std::coroutine_handle<> handle{};
        waiter_node* next = nullptr;
    };

public:
    async_wait_group() noexcept = default;
    ~async_wait_group() = default;

    async_wait_group(const async_wait_group&) = delete;
    auto operator=(const async_wait_group&) -> async_wait_group& = delete;

    /// 增加待完成任务计数
    void add(int n = 1) noexcept {
        count_.fetch_add(n, std::memory_order_relaxed);
    }

    /// 标记一个任务完成，归零时唤醒所有 wait() 等待者
    void done() noexcept {
        if (count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // 计数归零，唤醒所有等待者
            std::vector<std::coroutine_handle<>> to_resume;
            {
                std::lock_guard lock(mtx_);
                while (head_) {
                    auto* w = head_;
                    head_ = w->next;
                    to_resume.push_back(w->handle);
                }
                tail_ = nullptr;
            }
            for (auto h : to_resume)
                h.resume();
        }
    }

    // =========================================================================
    // wait — 等待计数归零
    // =========================================================================

    struct [[nodiscard]] wait_awaitable {
        async_wait_group& wg_;
        waiter_node node_;

        explicit wait_awaitable(async_wait_group& wg) noexcept : wg_(wg) {}

        auto await_ready() noexcept -> bool {
            return wg_.count_.load(std::memory_order_acquire) <= 0;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            node_.handle = h;
            node_.next = nullptr;
            std::lock_guard lock(wg_.mtx_);
            // 再次检查（可能在 suspend 前已归零）
            if (wg_.count_.load(std::memory_order_acquire) <= 0) {
                h.resume();
                return;
            }
            // 加入等待队列
            if (!wg_.tail_) {
                wg_.head_ = wg_.tail_ = &node_;
            } else {
                wg_.tail_->next = &node_;
                wg_.tail_ = &node_;
            }
        }

        void await_resume() noexcept {}
    };

    auto wait() noexcept -> wait_awaitable {
        return wait_awaitable{*this};
    }

    /// 当前剩余计数（仅供监控）
    [[nodiscard]] auto count() const noexcept -> int {
        return count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> count_{0};
    std::mutex mtx_;  // 保护等待队列
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
};

} // namespace cnetmod
