module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.timer;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.coro.cancel;

namespace cnetmod {

/// A monotonic, copyable time budget that can be passed through every layer
/// of a request. The default value is unlimited.
export class deadline final
{
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using duration = clock::duration;

    constexpr deadline() noexcept = default;
    explicit constexpr deadline(time_point value) noexcept : value_(value) {}

    [[nodiscard]] static auto after(duration value) noexcept -> deadline
    {
        return deadline{clock::now() + value};
    }

    [[nodiscard]] static constexpr auto at(time_point value) noexcept -> deadline
    {
        return deadline{value};
    }

    [[nodiscard]] constexpr auto is_unlimited() const noexcept -> bool
    {
        return value_ == time_point::max();
    }

    [[nodiscard]] auto expired() const noexcept -> bool
    {
        return !is_unlimited() && clock::now() >= value_;
    }

    [[nodiscard]] auto remaining() const noexcept -> duration
    {
        if (is_unlimited())
            return duration::max();
        const auto now = clock::now();
        return now >= value_ ? duration::zero() : value_ - now;
    }

    /// Make a child budget which can never outlive its parent.
    [[nodiscard]] constexpr auto constrain(deadline child) const noexcept -> deadline
    {
        if (is_unlimited())
            return child;
        if (child.is_unlimited() || child.value_ > value_)
            return *this;
        return child;
    }

    [[nodiscard]] constexpr auto at_time() const noexcept -> deadline::time_point
    {
        return value_;
    }

private:
    deadline::time_point value_ = deadline::time_point::max();
};

// =============================================================================
// steady_timer — Low-Precision Timer (bound to io_context)
// =============================================================================

/// Async timer based on io_context
/// Uses platform-native timers (timerfd / EVFILT_TIMER / IOCP timer / io_uring timeout)
export class steady_timer
{
public:
    explicit steady_timer(io_context& ctx) noexcept;

    /// Async wait for specified duration
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>;

    [[nodiscard]] auto context() noexcept -> io_context&;

private:
    io_context* ctx_;
};

// =============================================================================
// high_resolution_timer — High-Precision Timer
// =============================================================================

/// High-precision timer, supports time_point waiting
export class high_resolution_timer
{
public:
    explicit high_resolution_timer(io_context& ctx) noexcept;

    /// Async wait until specified time point
    auto async_wait_until(std::chrono::steady_clock::time_point deadline)
        -> task<std::expected<void, std::error_code>>;

    /// Async wait for specified duration
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>;

    [[nodiscard]] auto context() noexcept -> io_context&;

private:
    io_context* ctx_;
};

// =============================================================================
// async_sleep — Convenience functions
// =============================================================================

/// Throwing convenience wrapper over `async_timer_wait()`.
/// Use `async_timer_wait()` or timer objects when you want explicit
/// `std::expected`-based error handling.
export auto async_sleep(io_context& ctx,
    std::chrono::steady_clock::duration duration)
    -> task<void>;

/// Convenience: async_sleep until specified time point
export auto async_sleep_until(io_context& ctx,
    std::chrono::steady_clock::time_point tp)
    -> task<void>;

// =============================================================================
// with_timeout — Add timeout to cancellable async operations
// =============================================================================

namespace detail {

    template <class> struct deadline_operation;

    template <class T>
    struct deadline_operation<task<std::expected<T, std::error_code>>>
    {
        using value_type = T;
    };

    auto deadline_timer_task(io_context& ctx, deadline value,
        cancel_token& timer_token, cancel_token& op_token) -> task<int>;

    /// Operation side: cancel timer after completion
    template <typename T>
    auto timeout_op_wrapper(task<std::expected<T, std::error_code>> op,
        cancel_token& timer_token)
        -> task<std::expected<T, std::error_code>>
    {
        auto result = co_await std::move(op);
        timer_token.cancel();
        co_return std::move(result);
    }

} // namespace detail

export template <typename T>
auto with_deadline(io_context& ctx, deadline value,
    task<std::expected<T, std::error_code>> op, cancel_token& op_token)
    -> task<std::expected<T, std::error_code>>;

/// Factory form for token-aware operations. It creates a fresh token for the
/// one downstream operation, avoiding accidental token sharing across fan-out.
export template <class Factory>
requires std::invocable<Factory, cancel_token&>
      && requires {
          typename detail::deadline_operation<std::remove_cvref_t<
              std::invoke_result_t<Factory, cancel_token&>>>::value_type;
      }
auto with_deadline(io_context& ctx, deadline value, Factory&& factory)
    -> task<std::expected<typename detail::deadline_operation<std::remove_cvref_t<
        std::invoke_result_t<Factory, cancel_token&>>>::value_type, std::error_code>>
{
    using value_type = typename detail::deadline_operation<std::remove_cvref_t<
        std::invoke_result_t<Factory, cancel_token&>>>::value_type;
    cancel_token token;
    co_return co_await with_deadline<value_type>(ctx, value,
        std::invoke(std::forward<Factory>(factory), token), token);
}

/// Add timeout to a cancellable async operation that already returns
/// `task<std::expected<T, std::error_code>>`.
/// Usage:
///   cancel_token token;
///   auto r = co_await with_timeout(ctx, 5s,
///       async_read(ctx, sock, buf, token), token);
///
/// After timeout, the wrapped operation is cancelled via `cancel_token` and
/// returns `std::errc::timed_out`. Existing code using `with_timeout` gains
/// the same cancellation-cause distinction as `with_deadline`.
export template <typename T>
auto with_timeout(io_context& ctx,
    std::chrono::steady_clock::duration timeout,
    task<std::expected<T, std::error_code>> op,
    cancel_token& op_token)
    -> task<std::expected<T, std::error_code>>
{
    co_return co_await with_deadline(ctx, deadline::after(timeout),
        std::move(op), op_token);
}

/// Add an absolute deadline to a cancellable I/O operation. The deadline is
/// propagated as a value, so nested code can constrain it rather than starting
/// independent relative timers. A deadline result is normalized to timed_out;
/// explicit caller cancellation remains operation_aborted.
template <typename T>
auto with_deadline(io_context& ctx, deadline value,
    task<std::expected<T, std::error_code>> op, cancel_token& op_token)
    -> task<std::expected<T, std::error_code>>
{
    if (value.expired())
    {
        op_token.cancel_due_to_deadline();
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    }
    if (value.is_unlimited())
        co_return co_await std::move(op);

    cancel_token timer_token;
    auto op_task = detail::timeout_op_wrapper<T>(std::move(op), timer_token);
    auto timer_task = detail::deadline_timer_task(ctx, value, timer_token, op_token);
    auto [result, ignored] = co_await when_all(std::move(op_task), std::move(timer_task));
    (void)ignored;
    if (op_token.reason() == cancellation_reason::deadline_exceeded)
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    co_return std::move(result);
}

} // namespace cnetmod
