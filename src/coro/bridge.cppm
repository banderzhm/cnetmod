/**
 * @file bridge.cppm
 * @brief Coroutine bridge tools — Connect blocking world, stdexec sender, external coroutine types
 *
 * Three core tools:
 *
 * 1. blocking_invoke(pool, io, fn) → task<R>
 *    Offload blocking calls to stdexec thread pool, automatically switch back to io_context when complete.
 *    Suitable for RabbitMQ, gRPC synchronous clients, traditional database drivers, etc.
 *
 * 2. await_sender<T>(sender) → sender_awaitable
 *    co_await any stdexec sender within task<T> coroutine.
 *    Suitable for interoperability with other sender/receiver libraries.
 *
 * 3. from_awaitable(awaitable) → task<R>
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
 *   // 2. co_await stdexec sender
 *   auto val = co_await await_sender<int>(
 *       stdexec::then(sched.schedule(), [] { return 42; }));
 *
 *   // 3. Wrap third-party coroutine
 *   auto result = co_await from_awaitable(third_party_async_call());
 */
module;

#include <cnetmod/config.hpp>
#include <new>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

export module cnetmod.coro.bridge;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;

namespace cnetmod {

// =============================================================================
// blocking_invoke — Offload blocking calls to stdexec thread pool
// =============================================================================

/// Execute blocking callable on stdexec thread pool, switch back to io_context event loop thread when complete.
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
    requires std::invocable<F>
          && (!std::is_void_v<std::invoke_result_t<F>>)
auto blocking_invoke_impl(exec::static_thread_pool& pool, io_context& io, F fn)
    -> task<std::invoke_result_t<F>>
{
    using R = std::invoke_result_t<F>;
    co_await pool_post_awaitable{pool};
    R result = fn();
    co_await post_awaitable{io};
    co_return std::move(result);
}

template <typename F>
    requires std::invocable<F>
          && std::is_void_v<std::invoke_result_t<F>>
auto blocking_invoke_impl(exec::static_thread_pool& pool, io_context& io, F fn)
    -> task<void>
{
    co_await pool_post_awaitable{pool};
    fn();
    co_await post_awaitable{io};
}

} // namespace detail

/// Non-coroutine wrapper: calls detail layer coroutine implementation, avoids MSVC IFC export coroutine template ICE
/// Uses auto return type, avoids MSVC 14.50 ICE when serializing task<...> dependent type to IFC
export template <typename F>
    requires std::invocable<std::decay_t<F>>
          && (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>)
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn) {
    return detail::blocking_invoke_impl(
        pool, io, std::decay_t<F>(std::forward<F>(fn)));
}

/// void return value specialization
export template <typename F>
    requires std::invocable<std::decay_t<F>>
          && std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn) {
    return detail::blocking_invoke_impl(
        pool, io, std::decay_t<F>(std::forward<F>(fn)));
}

// =============================================================================
// sender_awaitable — co_await any stdexec sender in task<T>
// =============================================================================
//
// sender_awaitable is an awaitable object embedded in the coroutine frame.
// When co_await:
//   1. await_suspend: connect(sender, bridge_receiver) → op_state, then start
//   2. When sender completes: bridge_receiver stores result and resumes coroutine
//   3. await_resume: returns stored result
//
// op_state uses placement storage (awaitable is in coroutine frame, survives during suspension)
// =============================================================================

namespace detail {

/// Bridge receiver for sender_awaitable (non-void)
template <typename T>
struct bridge_receiver {
    using receiver_concept = stdexec::receiver_t;

    std::optional<T>* result_;
    std::exception_ptr* error_;
    std::coroutine_handle<>* coro_;

    void set_value(T val) noexcept {
        result_->emplace(std::move(val));
        coro_->resume();
    }

    void set_error(std::exception_ptr e) noexcept {
        *error_ = std::move(e);
        coro_->resume();
    }

    void set_stopped() noexcept {
        *error_ = std::make_exception_ptr(
            std::runtime_error("sender stopped"));
        coro_->resume();
    }

    struct env {};
    auto get_env() const noexcept -> env { return {}; }
};

/// void specialization
template <>
struct bridge_receiver<void> {
    using receiver_concept = stdexec::receiver_t;

    bool* done_;
    std::exception_ptr* error_;
    std::coroutine_handle<>* coro_;

    void set_value() noexcept {
        *done_ = true;
        coro_->resume();
    }

    void set_error(std::exception_ptr e) noexcept {
        *error_ = std::move(e);
        coro_->resume();
    }

    void set_stopped() noexcept {
        *error_ = std::make_exception_ptr(
            std::runtime_error("sender stopped"));
        coro_->resume();
    }

    struct env {};
    auto get_env() const noexcept -> env { return {}; }
};

} // namespace detail

/// co_await a stdexec sender (non-void result)
///
/// T = value type sent when sender completes
/// Sender = stdexec sender type
///
/// Usage:
///   auto v = co_await sender_awaitable<int, decltype(sndr)>{std::move(sndr)};
///   // Or use await_sender<int>(sndr) factory function
template <typename T, typename Sender>
struct sender_awaitable {
    using receiver_t = detail::bridge_receiver<T>;
    using op_t = stdexec::connect_result_t<Sender, receiver_t>;

    Sender sender_;
    std::coroutine_handle<> coro_;
    std::optional<T> result_;
    std::exception_ptr error_;
    alignas(op_t) std::byte op_storage_[sizeof(op_t)];

    explicit sender_awaitable(Sender sndr)
        : sender_(std::move(sndr)) {}

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        coro_ = h;
        auto* op = new (op_storage_) op_t(
            stdexec::connect(std::move(sender_),
                             receiver_t{&result_, &error_, &coro_}));
        stdexec::start(*op);
    }

    auto await_resume() -> T {
        // Destroy op_state
        std::launder(reinterpret_cast<op_t*>(op_storage_))->~op_t();
        if (error_)
            std::rethrow_exception(error_);
        return std::move(*result_);
    }
};

/// void sender specialization
template <typename Sender>
struct sender_awaitable<void, Sender> {
    using receiver_t = detail::bridge_receiver<void>;
    using op_t = stdexec::connect_result_t<Sender, receiver_t>;

    Sender sender_;
    std::coroutine_handle<> coro_;
    bool done_ = false;
    std::exception_ptr error_;
    alignas(op_t) std::byte op_storage_[sizeof(op_t)];

    explicit sender_awaitable(Sender sndr)
        : sender_(std::move(sndr)) {}

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        coro_ = h;
        auto* op = new (op_storage_) op_t(
            stdexec::connect(std::move(sender_),
                             receiver_t{&done_, &error_, &coro_}));
        stdexec::start(*op);
    }

    void await_resume() {
        std::launder(reinterpret_cast<op_t*>(op_storage_))->~op_t();
        if (error_)
            std::rethrow_exception(error_);
    }
};

// =============================================================================
// await_sender — Factory function, automatically deduce Sender type
// =============================================================================

/// co_await await_sender<int>(some_sender)
/// T needs to be explicitly specified (sender's value type)
/// Uses auto return type, avoids MSVC 14.50 ICE when serializing sender_awaitable<...> to IFC
export template <typename T, typename Sender>
auto await_sender(Sender&& sndr) {
    return sender_awaitable<T, std::decay_t<Sender>>{
        std::forward<Sender>(sndr)};
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
auto from_awaitable_impl(Awaitable aw) -> task<T> {
    if constexpr (std::is_void_v<T>) {
        co_await std::move(aw);
    } else {
        co_return co_await std::move(aw);
    }
}
} // namespace detail

/// Uses auto return type, avoids MSVC 14.50 ICE when serializing task<T> to IFC
export template <typename T, typename Awaitable>
auto from_awaitable(Awaitable&& aw) {
    // Non-coroutine wrapper: calls detail layer coroutine implementation, avoids MSVC IFC export coroutine template ICE
    return detail::from_awaitable_impl<T>(std::forward<Awaitable>(aw));
}

} // namespace cnetmod
