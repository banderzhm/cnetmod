/**
 * @file retry.cppm
 * @brief Async retry policy — Exponential backoff with optional jitter
 *
 * Usage example:
 *   import cnetmod.coro.retry;
 *
 *   // Retry up to 3 times with exponential backoff
 *   auto result = co_await retry(ctx, {.max_attempts = 3}, [&]() {
 *       return do_something();  // returns task<expected<T, E>>
 *   });
 *
 *   // Custom retry options
 *   auto result = co_await retry(ctx, {
 *       .max_attempts = 5,
 *       .initial_delay = 200ms,
 *       .max_delay = 10s,
 *       .multiplier = 3.0,
 *       .jitter = true,
 *   }, operation);
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.retry;

import std;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;

namespace cnetmod {

// =============================================================================
// retry_options — Retry configuration
// =============================================================================

export struct retry_options
{
    std::uint32_t max_attempts = 3; ///< Max attempts (including first try)
    std::chrono::steady_clock::duration
        initial_delay = std::chrono::milliseconds(100); ///< Delay after first failure
    std::chrono::steady_clock::duration
        max_delay = std::chrono::seconds(5); ///< Max delay cap
    double multiplier = 2.0;                 ///< Backoff multiplier
    bool jitter = true;                      ///< Add random ±25% jitter to delay
};

// =============================================================================
// retry_backoff — Internal delay state machine (jitter + exponential growth)
// =============================================================================

class retry_backoff
{
public:
    explicit retry_backoff(const retry_options& opts) noexcept
        : delay_(opts.initial_delay), max_delay_(opts.max_delay), multiplier_(opts.multiplier), jitter_(opts.jitter)
    {
    }

    auto sleep(io_context& ctx) -> task<void>
    {
        auto actual = delay_;
        if (jitter_)
        {
            static thread_local std::mt19937 rng{std::random_device{}()};
            const auto base_us = std::chrono::duration_cast<std::chrono::microseconds>(delay_).count();
            std::uniform_int_distribution<long long> dist(
                static_cast<long long>(base_us * 0.75),
                static_cast<long long>(base_us * 1.25));
            actual = std::chrono::microseconds(dist(rng));
        }
        co_await async_sleep(ctx, actual);
        advance();
    }

private:
    void advance() noexcept
    {
        const auto next_us = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::microseconds>(delay_).count() * multiplier_);
        delay_ = std::chrono::microseconds(next_us);
        if (delay_ > max_delay_)
            delay_ = max_delay_;
    }

    std::chrono::steady_clock::duration delay_;
    std::chrono::steady_clock::duration max_delay_;
    double multiplier_;
    bool jitter_;
};

// =============================================================================
// retry — Retry with exponential backoff (for expected<T, E> returning functions)
// =============================================================================

namespace detail {
    template <typename T>
    struct task_expected_error;

    template <typename V, typename E>
    struct task_expected_error<task<std::expected<V, E>>>
    {
        using type = E;
    };

    template <typename T>
    inline constexpr bool is_task_expected_void = false;

    template <typename E>
    inline constexpr bool is_task_expected_void<task<std::expected<void, E>>> = true;
} // namespace detail

/// Retry an async operation that returns task<expected<T, E>>
/// Retries on unexpected (error) result, returns first success or last error
export template <typename T, typename E, typename Fn>
requires std::invocable<Fn> &&
             std::same_as<std::invoke_result_t<Fn>, task<std::expected<T, E>>>
auto retry(io_context& ctx, retry_options opts, Fn fn)
    -> task<std::expected<T, E>>
{
    std::expected<T, E> last_result = std::unexpected(E{});
    retry_backoff backoff(opts);

    for (std::uint32_t attempt = 0; attempt < opts.max_attempts; ++attempt)
    {
        last_result = co_await fn();
        if (last_result.has_value())
            co_return std::move(last_result);
        if (attempt + 1 >= opts.max_attempts)
            break;
        co_await backoff.sleep(ctx);
    }

    co_return std::move(last_result);
}

/// Overload for task<expected<void, E>> — Clang cannot deduce T=void in the primary template
export template <typename Fn>
requires std::invocable<Fn> &&
             detail::is_task_expected_void<std::invoke_result_t<Fn>>
auto retry(io_context& ctx, retry_options opts, Fn fn)
    -> std::invoke_result_t<Fn>
{
    using result_type = std::invoke_result_t<Fn>;
    using E = typename detail::task_expected_error<result_type>::type;

    std::expected<void, E> last_result = std::unexpected(E{});
    retry_backoff backoff(opts);

    for (std::uint32_t attempt = 0; attempt < opts.max_attempts; ++attempt)
    {
        last_result = co_await fn();
        if (last_result.has_value())
            co_return std::move(last_result);
        if (attempt + 1 >= opts.max_attempts)
            break;
        co_await backoff.sleep(ctx);
    }

    co_return std::move(last_result);
}

/// Retry an async operation that returns task<T> and signals failure via exceptions
/// Retries on exception, returns first success or rethrows last exception
export template <typename Fn>
requires std::invocable<Fn>
auto retry_throwing(io_context& ctx, retry_options opts, Fn fn)
    -> std::invoke_result_t<Fn>
{
    std::exception_ptr last_exception;
    retry_backoff backoff(opts);

    for (std::uint32_t attempt = 0; attempt < opts.max_attempts; ++attempt)
    {
        try
        {
            co_return co_await fn();
        }
        catch (...)
        {
            last_exception = std::current_exception();
        }
        if (attempt + 1 >= opts.max_attempts)
            break;
        co_await backoff.sleep(ctx);
    }

    std::rethrow_exception(last_exception);
}

} // namespace cnetmod
