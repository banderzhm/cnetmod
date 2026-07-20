module cnetmod.coro.timer;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

namespace cnetmod
{
    steady_timer::steady_timer(io_context& ctx) noexcept : ctx_(&ctx)
    {
    }

    auto steady_timer::async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_timer_wait(*ctx_, duration);
    }

    auto steady_timer::context() noexcept -> io_context& { return *ctx_; }

    high_resolution_timer::high_resolution_timer(io_context& ctx) noexcept : ctx_(&ctx)
    {
    }

    auto high_resolution_timer::async_wait_until(std::chrono::steady_clock::time_point deadline)
        -> task<std::expected<void, std::error_code>>
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) co_return std::expected<void, std::error_code>{};
        co_return co_await async_timer_wait(*ctx_, deadline - now);
    }

    auto high_resolution_timer::async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_timer_wait(*ctx_, duration);
    }

    auto high_resolution_timer::context() noexcept -> io_context& { return *ctx_; }

    auto async_sleep(io_context& ctx,
                    std::chrono::steady_clock::duration duration) -> task<void>
    {
        auto result = co_await async_timer_wait(ctx, duration);
        if (!result) {
            throw std::system_error(result.error());
        }
    }

    auto async_sleep_until(io_context& ctx,
                          std::chrono::steady_clock::time_point deadline) -> task<void>
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return;
        }
        co_await async_sleep(ctx, deadline - now);
    }

    namespace detail {

    auto timeout_timer_task(io_context& ctx,
                            std::chrono::steady_clock::duration duration,
                            cancel_token& timer_token,
                            cancel_token& operation_token) -> task<int>
    {
        (void)co_await async_timer_wait(ctx, duration, timer_token);
        if (!timer_token.is_cancelled()) {
            operation_token.cancel();
        }
        co_return 0;
    }

    } // namespace detail
} // namespace cnetmod
