/// cnetmod example — Blocking Bridge Demo
///
/// 演示如何在协程中安全调用阻塞/多线程 API（如 RabbitMQ、gRPC 同步客户端）
/// 以及如何 co_await stdexec sender。
///
/// 三种桥接模式:
///   1. blocking_invoke — 阻塞调用卸载到线程池，不阻塞 io_context
///   2. await_sender    — co_await 任意 stdexec sender
///   3. from_awaitable  — 包装第三方协程库的 awaitable 类型

import std;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

using namespace cnetmod;

// =============================================================================
// 模拟第三方阻塞 API（如 RabbitMQ C 客户端）
// =============================================================================

namespace fake_rabbitmq {

/// 模拟阻塞消费消息（实际会阻塞线程等待网络 I/O）
auto consume(std::string_view queue, int timeout_ms) -> std::string {
    // 模拟阻塞等待
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return std::format("[{}] message: hello from {}", queue,
                       std::this_thread::get_id());
}

/// 模拟阻塞发布消息
void publish(std::string_view exchange, std::string_view msg) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::println("  [rabbitmq] published to '{}': {} (thread {})",
                 exchange, msg, std::this_thread::get_id());
}

} // namespace fake_rabbitmq

// =============================================================================
// 模拟阻塞数据库查询
// =============================================================================

namespace fake_db {

struct row {
    int id;
    std::string name;
};

auto query([[maybe_unused]] std::string_view sql) -> std::vector<row> {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return {{1, "Alice"}, {2, "Bob"}, {3, "Charlie"}};
}

} // namespace fake_db

// =============================================================================
// 模拟第三方协程库的 awaitable 类型
// =============================================================================

/// 一个简单的 "外部" awaitable —— 模拟第三方库返回的类型
struct foreign_awaitable {
    int value_;

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        // 模拟异步操作：直接在新线程中完成
        std::jthread([h] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            h.resume();
        }).detach();
    }

    auto await_resume() const noexcept -> int { return value_; }
};

// =============================================================================
// Demo 1: blocking_invoke — 阻塞 API 桥接
// =============================================================================

auto demo_blocking_invoke(server_context& ctx) -> task<void> {
    std::println("\n=== Demo 1: blocking_invoke ===");
    std::println("  io_context thread: {}", std::this_thread::get_id());

    auto& pool = ctx.pool();  // exec::static_thread_pool (auto& 推导)
    auto& io   = ctx.accept_io();

    // 1a. 消费 RabbitMQ 消息（阻塞操作在线程池执行）
    auto msg = co_await blocking_invoke(pool, io, [] {
        return fake_rabbitmq::consume("orders", 200);
    });
    std::println("  received: {} (back on thread {})",
                 msg, std::this_thread::get_id());

    // 1b. 发布消息（void 返回值）
    co_await blocking_invoke(pool, io, [] {
        fake_rabbitmq::publish("notifications", "order_created");
    });
    std::println("  publish done (back on thread {})",
                 std::this_thread::get_id());

    // 1c. 数据库查询
    auto rows = co_await blocking_invoke(pool, io, [] {
        return fake_db::query("SELECT * FROM users");
    });
    std::println("  db query returned {} rows:", rows.size());
    for (auto& r : rows)
        std::println("    id={}, name={}", r.id, r.name);
}

// =============================================================================
// Demo 2: blocking_invoke + when_all — 并发执行多个阻塞操作
// =============================================================================

auto demo_concurrent_blocking(server_context& ctx) -> task<void> {
    std::println("\n=== Demo 2: concurrent blocking_invoke (when_all) ===");

    auto& pool = ctx.pool();
    auto& io   = ctx.accept_io();

    auto start = std::chrono::steady_clock::now();

    // 两个阻塞操作并发执行：总时间 ≈ max(200, 100) 而不是 200+100
    auto [msg, rows] = co_await when_all(
        blocking_invoke(pool, io, [] {
            return fake_rabbitmq::consume("events", 200);
        }),
        blocking_invoke(pool, io, [] {
            return fake_db::query("SELECT * FROM orders");
        })
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::println("  rabbitmq: {}", msg);
    std::println("  db: {} rows", rows.size());
    std::println("  total time: {}ms (concurrent, not sequential!)", elapsed);
}

// =============================================================================
// Demo 3: await_sender — co_await stdexec sender（task_sender 互操作）
// =============================================================================

/// 模拟一个返回 stdexec sender 的第三方 API
auto compute_on_sender(int x) -> task<int> {
    co_return x * x;
}

auto greet_sender() -> task<std::string> {
    co_return std::string{"hello from task_sender"};
}

auto fire_and_forget() -> task<void> {
    std::println("  [sender] fire-and-forget executed on thread: {}",
                 std::this_thread::get_id());
    co_return;
}

auto demo_await_sender() -> task<void> {
    std::println("\n=== Demo 3: await_sender (stdexec sender interop) ===");

    // task_sender<T> 是标准的 stdexec sender
    // await_sender<T> 可以 co_await 任意 sender

    // co_await 一个 task_sender<int>
    auto val = co_await await_sender<int>(as_sender(compute_on_sender(7)));
    std::println("  sender result: {}", val);

    // co_await 一个 task_sender<string>
    auto msg = co_await await_sender<std::string>(as_sender(greet_sender()));
    std::println("  sender msg: {}", msg);

    // co_await 一个 void sender
    co_await await_sender<void>(as_sender(fire_and_forget()));
    std::println("  void sender done");
}

// =============================================================================
// Demo 4: from_awaitable — 包装第三方 awaitable
// =============================================================================

auto demo_from_awaitable() -> task<void> {
    std::println("\n=== Demo 4: from_awaitable (foreign awaitable) ===");

    // 将外部 awaitable 包装为 cnetmod task<int>
    auto result = co_await from_awaitable<int>(foreign_awaitable{99});
    std::println("  foreign awaitable result: {}", result);

    // 也可以直接 co_await（task<T> 的 promise 天然支持任意 awaitable）
    auto result2 = co_await foreign_awaitable{77};
    std::println("  direct co_await result: {}", result2);
}

// =============================================================================
// Main
// =============================================================================

auto run_all(server_context& ctx) -> task<void> {
    co_await demo_blocking_invoke(ctx);
    co_await demo_concurrent_blocking(ctx);
    co_await demo_await_sender();
    co_await demo_from_awaitable();

    std::println("\n=== All demos complete ===");
    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Blocking Bridge Demo ===");
    std::println("Main thread: {}", std::this_thread::get_id());

    // server_context 封装了 io_context + stdexec thread pool
    server_context ctx(1 /*workers*/, 4 /*pool_threads*/);

    spawn(ctx.accept_io(), run_all(ctx));
    ctx.run();  // 阻塞直到 stop()

    return 0;
}
