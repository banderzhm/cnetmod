/**
 * @file graceful_shutdown.cppm
 * @brief 优雅关停 — 信号处理 + 在途请求等待
 *
 * 注册 SIGINT/SIGTERM（Windows: SetConsoleCtrlHandler），
 * 收到信号后唤醒等待者，配合 drain 等待在途请求完成。
 *
 * 使用示例:
 *   import cnetmod.middleware.graceful_shutdown;
 *
 *   cnetmod::shutdown_handler sh;
 *   sh.install();  // 注册信号处理
 *
 *   // 在途请求追踪（中间件自动 +1/-1）
 *   svr.use(sh.track_middleware());
 *
 *   // 主协程等待关停信号
 *   auto sleeper = [&](auto dur) { return async_sleep(io, dur); };
 *   co_await sh.wait_for_signal(sleeper);
 *   logger::info("Shutting down...");
 *
 *   // 等待在途请求完成（最多 5 秒）
 *   co_await sh.drain(sleeper, std::chrono::seconds{5});
 *
 *   srv.stop();
 *   ctx.stop();
 */
module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <signal.h>
#endif

export module cnetmod.middleware.graceful_shutdown;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// shutdown_handler — 优雅关停控制器
// =============================================================================

export class shutdown_handler {
public:
    shutdown_handler() noexcept = default;

    /// 注册信号处理器（SIGINT/SIGTERM / Windows Ctrl+C）
    /// 收到信号后设置 signaled_ 并通知等待者
    void install() noexcept {
        instance_ = this;
#ifdef CNETMOD_PLATFORM_WINDOWS
        SetConsoleCtrlHandler(win_handler, TRUE);
#else
        struct sigaction sa{};
        sa.sa_handler = unix_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
#endif
    }

    /// 是否已收到关停信号
    [[nodiscard]] auto is_signaled() const noexcept -> bool {
        return signaled_.load(std::memory_order_acquire);
    }

    /// 当前在途请求数
    [[nodiscard]] auto in_flight() const noexcept -> std::int64_t {
        return in_flight_.load(std::memory_order_relaxed);
    }

    // =========================================================================
    // wait_for_signal — 协程等待关停信号
    // =========================================================================

    /// 通过轮询实现信号等待（避免跨平台 eventfd/pipe 复杂性）
    /// 100ms 检查一次，收到信号后返回
    ///
    /// @param sleep_fn 异步睡眠函数，签名: (duration) -> task<void>
    ///   典型用法: [&](auto dur) { return async_sleep(io, dur); }
    template<typename SleepFn>
    auto wait_for_signal(SleepFn sleep_fn) -> task<void> {
        while (!signaled_.load(std::memory_order_acquire)) {
            co_await sleep_fn(std::chrono::milliseconds{100});
        }
        logger::info("Shutdown signal received");
    }

    // =========================================================================
    // drain — 等待在途请求完成
    // =========================================================================

    /// 等待所有在途请求完成，或超时后强制返回
    /// @param sleep_fn 异步睡眠函数，同 wait_for_signal
    /// @param timeout 最大等待时间
    /// @return true 表示正常排空，false 表示超时
    template<typename SleepFn>
    auto drain(SleepFn sleep_fn, std::chrono::steady_clock::duration timeout)
        -> task<bool>
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (in_flight_.load(std::memory_order_acquire) > 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                logger::warn("Drain timeout, {} requests still in-flight",
                             in_flight_.load(std::memory_order_relaxed));
                co_return false;
            }
            co_await sleep_fn(std::chrono::milliseconds{50});
        }
        logger::info("All in-flight requests drained");
        co_return true;
    }

    // =========================================================================
    // track_middleware — 在途请求追踪中间件
    // =========================================================================

    /// 放在中间件链中，自动追踪在途请求数
    /// 关停信号后的新请求直接返回 503
    auto track_middleware() -> http::middleware_fn {
        return [this](http::request_context& ctx,
                      http::next_fn next) -> task<void>
        {
            // 如果已收到关停信号，拒绝新请求
            if (signaled_.load(std::memory_order_acquire)) {
                ctx.resp().set_header("Connection", "close");
                ctx.json(http::status::service_unavailable,
                    R"({"error":"server is shutting down"})");
                co_return;
            }

            in_flight_.fetch_add(1, std::memory_order_relaxed);
            try {
                co_await next();
            } catch (...) {
                in_flight_.fetch_sub(1, std::memory_order_relaxed);
                throw;
            }
            in_flight_.fetch_sub(1, std::memory_order_relaxed);
        };
    }

private:
    std::atomic<bool> signaled_{false};
    std::atomic<std::int64_t> in_flight_{0};

    // 全局实例指针（信号处理器只能是静态/全局函数）
    static inline shutdown_handler* instance_ = nullptr;

    void signal() noexcept {
        signaled_.store(true, std::memory_order_release);
    }

#ifdef CNETMOD_PLATFORM_WINDOWS
    static BOOL WINAPI win_handler(DWORD ctrl_type) {
        if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT
            || ctrl_type == CTRL_CLOSE_EVENT)
        {
            if (instance_) instance_->signal();
            return TRUE;
        }
        return FALSE;
    }
#else
    static void unix_handler(int /*sig*/) {
        if (instance_) instance_->signal();
    }
#endif
};

} // namespace cnetmod
