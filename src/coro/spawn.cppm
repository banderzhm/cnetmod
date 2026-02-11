module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.spawn;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

// =============================================================================
// detached_task — fire-and-forget 协程类型
// =============================================================================

/// 启动后立即执行，完成后自动销毁帧，无需外部管理生命周期
struct detached_task {
    struct promise_type {
        auto get_return_object() noexcept -> detached_task { return {}; }
        auto initial_suspend() noexcept -> std::suspend_never { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
        auto final_suspend() noexcept -> std::suspend_never { return {}; }
    };
};

// =============================================================================
// spawn — 在 io_context 上启动 fire-and-forget 协程
// =============================================================================

/// spawn(ctx, task) — 将 task 投递到 io_context 事件循环执行
/// detached_task 拥有 task 的 co_await，task 完成后帧自动销毁
export void spawn(io_context& ctx, task<void> t) {
    [](io_context& c, task<void> inner) -> detached_task {
        co_await post_awaitable{c};     // 切换到事件循环线程
        co_await std::move(inner);      // 驱动 task 执行
    }(ctx, std::move(t));
}

} // namespace cnetmod
