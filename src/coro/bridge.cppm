/**
 * @file bridge.cppm
 * @brief Coroutine bridge tools — Connect blocking world and external coroutine types
 *
 * Two core tools:
 *
 * 1. blocking_invoke(pool, io, fn) → task<R>
 *    Offload blocking calls to cnetmod's thread pool, automatically switch back to io_context when complete.
 *    Suitable for RabbitMQ, gRPC synchronous clients, traditional database drivers, etc.
 *
 * 2. from_awaitable(awaitable) → task<R>
 *    Wrap any C++20 awaitable (third-party coroutine library task types) as cnetmod task<T>.
 *
 * Usage examples:
 *   import cnetmod.coro.bridge;
 *
 *   // 1. Blocking operation bridge
 *   auto msg = co_await blocking_invoke(pool, io, [&] {
 *       return rabbitmq_client.consume("queue1");  // Blocking call
 *   });
 *
 *   // 2. Wrap third-party coroutine
 *   auto result = co_await from_awaitable(third_party_async_call());
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.bridge;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;

namespace cnetmod {

// =============================================================================
// blocking_invoke — Offload blocking calls to cnetmod thread pool
// =============================================================================

/// Execute a blocking callable on cnetmod's thread pool, then switch back to
/// the io_context event-loop thread.
///
/// Use cases:
///   - Synchronous consumption of message queues like RabbitMQ, Kafka
///   - gRPC synchronous client calls
///   - Traditional database drivers (non-async versions)
///   - Any third-party library that only provides multi-threaded/blocking API
///
/// Principle:
///   1. co_await pool_post_awaitable → coroutine suspends, resumes on thread pool thread
///   2. Execute fn() → blocking operation runs on thread pool thread, doesn't affect io_context
///   3. co_await post_awaitable → coroutine suspends, resumes on io_context thread
///   4. co_return result → caller gets result on io_context thread
///
/// Usage:
///   auto msg = co_await blocking_invoke(pool, io_ctx, [&] {
///       return rabbitmq.basic_consume("queue1", timeout_ms);
///   });
namespace detail {

    /// blocking_invoke coroutine implementation (not exported, avoids MSVC 14.50 "export coroutine template" ICE bug)
    template <typename F>
    requires std::invocable<F> && (!std::is_void_v<std::invoke_result_t<F>>)
    auto blocking_invoke_impl(thread_pool& pool, io_context& io, F fn)
        -> task<std::invoke_result_t<F>>
    {
        using R = std::invoke_result_t<F>;
        std::exception_ptr scheduling_error;
        try
        {
            co_await pool_post_awaitable{pool};
        }
        catch (...)
        {
            scheduling_error = std::current_exception();
        }
        if (scheduling_error)
        {
            co_await post_awaitable{io};
            std::rethrow_exception(scheduling_error);
        }
        std::optional<R> result;
        std::exception_ptr error;
        try
        {
            result.emplace(fn());
        }
        catch (...)
        {
            error = std::current_exception();
        }
        co_await post_awaitable{io};
        if (error)
            std::rethrow_exception(error);
        co_return std::move(*result);
    }

    template <typename F>
    requires std::invocable<F> && std::is_void_v<std::invoke_result_t<F>>
    auto blocking_invoke_impl(thread_pool& pool, io_context& io, F fn)
        -> task<void>
    {
        std::exception_ptr scheduling_error;
        try
        {
            co_await pool_post_awaitable{pool};
        }
        catch (...)
        {
            scheduling_error = std::current_exception();
        }
        if (scheduling_error)
        {
            co_await post_awaitable{io};
            std::rethrow_exception(scheduling_error);
        }
        std::exception_ptr error;
        try
        {
            fn();
        }
        catch (...)
        {
            error = std::current_exception();
        }
        co_await post_awaitable{io};
        if (error)
            std::rethrow_exception(error);
    }

} // namespace detail

/// Non-coroutine wrapper: calls detail layer coroutine implementation, avoids MSVC IFC export coroutine template ICE
/// Uses auto return type, avoids MSVC 14.50 ICE when serializing task<...> dependent type to IFC
export template <typename F>
requires std::invocable<std::decay_t<F>> && (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>)
auto blocking_invoke(thread_pool& pool, io_context& io, F&& fn)
{
    return detail::blocking_invoke_impl(
        pool, io, std::decay_t<F>(std::forward<F>(fn)));
}

/// void return value specialization
export template <typename F>
requires std::invocable<std::decay_t<F>> && std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
auto blocking_invoke(thread_pool& pool, io_context& io, F&& fn)
{
    return detail::blocking_invoke_impl(
        pool, io, std::decay_t<F>(std::forward<F>(fn)));
}

// =============================================================================
// from_awaitable — Wrap any C++20 awaitable as cnetmod task<T>
// =============================================================================

/// Wrap third-party coroutine library awaitable type as cnetmod::task<T>
///
/// T needs to be explicitly specified (awaitable's co_await result type)
///
/// Suitable for:
///   - task/future types returned by other coroutine libraries (e.g., folly::coro::Task)
///   - Any type that implements operator co_await()
///
/// Usage:
///   auto result = co_await from_awaitable<int>(third_party_call());
///   co_await from_awaitable<void>(third_party_fire_and_forget());
///
/// Implementation note: Exported from_awaitable is a regular (non-coroutine) wrapper function,
/// the actual coroutine implementation is in detail::from_awaitable_impl (not exported).
/// This is a workaround for MSVC 14.50 ICE bug with "exported coroutine templates".
namespace detail {
    template <typename T, typename Awaitable>
    auto from_awaitable_impl(Awaitable aw) -> task<T>
    {
        if constexpr (std::is_void_v<T>)
        {
            co_await std::move(aw);
        }
        else
        {
            co_return co_await std::move(aw);
        }
    }
} // namespace detail

/// Uses auto return type, avoids MSVC 14.50 ICE when serializing task<T> to IFC
export template <typename T, typename Awaitable>
auto from_awaitable(Awaitable&& aw)
{
    // Non-coroutine wrapper: calls detail layer coroutine implementation, avoids MSVC IFC export coroutine template ICE
    return detail::from_awaitable_impl<T>(std::forward<Awaitable>(aw));
}

} // namespace cnetmod
