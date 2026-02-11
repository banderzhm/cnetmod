module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.timer;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod {

// =============================================================================
// steady_timer — 低精度定时器（绑定 io_context）
// =============================================================================

/// 基于 io_context 的异步定时器
/// 使用平台原生定时器（timerfd / EVFILT_TIMER / IOCP timer / io_uring timeout）
export class steady_timer {
public:
    explicit steady_timer(io_context& ctx) noexcept
        : ctx_(&ctx) {}

    /// 异步等待指定时长
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_timer_wait(*ctx_, duration);
    }

    [[nodiscard]] auto context() noexcept -> io_context& { return *ctx_; }

private:
    io_context* ctx_;
};

// =============================================================================
// high_resolution_timer — 高精度定时器
// =============================================================================

/// 高精度定时器，支持 time_point 等待
export class high_resolution_timer {
public:
    explicit high_resolution_timer(io_context& ctx) noexcept
        : ctx_(&ctx) {}

    /// 异步等待到指定时间点
    auto async_wait_until(std::chrono::steady_clock::time_point deadline)
        -> task<std::expected<void, std::error_code>>
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            co_return std::expected<void, std::error_code>{};
        co_return co_await async_timer_wait(*ctx_, deadline - now);
    }

    /// 异步等待指定时长
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_timer_wait(*ctx_, duration);
    }

    [[nodiscard]] auto context() noexcept -> io_context& { return *ctx_; }

private:
    io_context* ctx_;
};

// =============================================================================
// async_sleep — 便捷函数
// =============================================================================

/// co_await async_sleep(ctx, 100ms);
/// 通过 io_context 原生定时器实现，不创建额外线程
export inline auto async_sleep(io_context& ctx,
                               std::chrono::steady_clock::duration duration)
    -> task<void>
{
    auto r = co_await async_timer_wait(ctx, duration);
    if (!r)
        throw std::system_error(r.error());
}

/// 便捷: async_sleep 到指定时间点
export inline auto async_sleep_until(io_context& ctx,
                                     std::chrono::steady_clock::time_point tp)
    -> task<void>
{
    auto now = std::chrono::steady_clock::now();
    if (now >= tp)
        co_return;
    co_await async_sleep(ctx, tp - now);
}

} // namespace cnetmod
