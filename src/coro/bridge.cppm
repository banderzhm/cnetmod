/**
 * @file bridge.cppm
 * @brief 协程桥接工具 — 连接阻塞世界、stdexec sender、外部协程类型
 *
 * 三大核心工具:
 *
 * 1. blocking_invoke(pool, io, fn) → task<R>
 *    将阻塞调用卸载到 stdexec 线程池，完成后自动切回 io_context。
 *    适用于 RabbitMQ、gRPC 同步客户端、传统数据库驱动等。
 *
 * 2. await_sender<T>(sender) → sender_awaitable
 *    在 task<T> 协程内 co_await 任意 stdexec sender。
 *    适用于与其他 sender/receiver 库的互操作。
 *
 * 3. from_awaitable(awaitable) → task<R>
 *    将任意 C++20 awaitable（第三方协程库的 task 类型）包装为 cnetmod task<T>。
 *
 * 使用示例:
 *   import cnetmod.coro.bridge;
 *
 *   // 1. 阻塞操作桥接
 *   auto msg = co_await blocking_invoke(pool, io, [&] {
 *       return rabbitmq_client.consume("queue1");  // 阻塞调用
 *   });
 *
 *   // 2. co_await stdexec sender
 *   auto val = co_await await_sender<int>(
 *       stdexec::then(sched.schedule(), [] { return 42; }));
 *
 *   // 3. 包装第三方协程
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
// blocking_invoke — 将阻塞调用卸载到 stdexec 线程池
// =============================================================================

/// 在 stdexec 线程池上执行阻塞 callable，完成后切回 io_context 事件循环线程。
///
/// 适用场景:
///   - RabbitMQ、Kafka 等消息队列的同步消费
///   - gRPC 同步客户端调用
///   - 传统数据库驱动（非异步版本）
///   - 任何只提供多线程/阻塞 API 的第三方库
///
/// 原理:
///   1. co_await pool_post_awaitable → 协程挂起，在线程池线程上恢复
///   2. 执行 fn() → 阻塞操作在线程池线程上运行，不影响 io_context
///   3. co_await post_awaitable → 协程挂起，在 io_context 线程上恢复
///   4. co_return result → 调用者在 io_context 线程上拿到结果
///
/// 用法:
///   auto msg = co_await blocking_invoke(pool, io_ctx, [&] {
///       return rabbitmq.basic_consume("queue1", timeout_ms);
///   });
namespace detail {

/// blocking_invoke 协程实现（非导出，避免 MSVC 14.50 「导出协程模板」ICE bug）
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

/// 非协程包装：调用 detail 层的协程实现，避免 MSVC IFC 导出协程模板 ICE
/// 使用 auto 返回类型，避免 MSVC 14.50 将 task<...> 依赖类型序列化到 IFC 时的 ICE
export template <typename F>
    requires std::invocable<std::decay_t<F>>
          && (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>)
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn) {
    return detail::blocking_invoke_impl(
        pool, io, std::decay_t<F>(std::forward<F>(fn)));
}

/// void 返回值特化
export template <typename F>
    requires std::invocable<std::decay_t<F>>
          && std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn) {
    return detail::blocking_invoke_impl(
        pool, io, std::decay_t<F>(std::forward<F>(fn)));
}

// =============================================================================
// sender_awaitable — 在 task<T> 中 co_await 任意 stdexec sender
// =============================================================================
//
// sender_awaitable 是一个 awaitable 对象，嵌入在协程帧中。
// 当 co_await 时:
//   1. await_suspend: connect(sender, bridge_receiver) → op_state，然后 start
//   2. sender 完成时: bridge_receiver 存储结果并 resume 协程
//   3. await_resume: 返回存储的结果
//
// op_state 使用 placement storage（awaitable 在协程帧中，suspension 期间存活）
// =============================================================================

namespace detail {

/// 用于 sender_awaitable 的 bridge receiver (non-void)
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

/// void 特化
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

/// co_await 一个 stdexec sender (non-void result)
///
/// T = sender 完成时发送的值类型
/// Sender = stdexec sender 类型
///
/// 用法:
///   auto v = co_await sender_awaitable<int, decltype(sndr)>{std::move(sndr)};
///   // 或使用 await_sender<int>(sndr) 工厂函数
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
        // 销毁 op_state
        std::launder(reinterpret_cast<op_t*>(op_storage_))->~op_t();
        if (error_)
            std::rethrow_exception(error_);
        return std::move(*result_);
    }
};

/// void sender 特化
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
// await_sender — 工厂函数，自动推导 Sender 类型
// =============================================================================

/// co_await await_sender<int>(some_sender)
/// T 需要显式指定（sender 的值类型）
/// 使用 auto 返回类型，避免 MSVC 14.50 将 sender_awaitable<...> 序列化到 IFC 时的 ICE
export template <typename T, typename Sender>
auto await_sender(Sender&& sndr) {
    return sender_awaitable<T, std::decay_t<Sender>>{
        std::forward<Sender>(sndr)};
}

// =============================================================================
// from_awaitable — 将任意 C++20 awaitable 包装为 cnetmod task<T>
// =============================================================================

/// 将第三方协程库的 awaitable 类型包装为 cnetmod::task<T>
///
/// T 需要显式指定（awaitable 的 co_await 结果类型）
///
/// 适用于:
///   - 其他协程库返回的 task/future 类型（如 folly::coro::Task）
///   - 任何实现了 operator co_await() 的类型
///
/// 用法:
///   auto result = co_await from_awaitable<int>(third_party_call());
///   co_await from_awaitable<void>(third_party_fire_and_forget());
///
/// 实现说明：导出的 from_awaitable 是普通（非协程）包装函数，
/// 真正的协程实现在 detail::from_awaitable_impl（不导出）。
/// 这是规避 MSVC 14.50 对「导出协程模板」ICE bug 的 workaround。
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

/// 使用 auto 返回类型，避免 MSVC 14.50 将 task<T> 序列化到 IFC 时的 ICE
export template <typename T, typename Awaitable>
auto from_awaitable(Awaitable&& aw) {
    // 非协程包装：调用 detail 层的协程实现，避免 MSVC IFC 导出协程模板 ICE
    return detail::from_awaitable_impl<T>(std::forward<Awaitable>(aw));
}

} // namespace cnetmod
