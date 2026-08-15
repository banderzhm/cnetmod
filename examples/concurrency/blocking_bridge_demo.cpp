/// cnetmod example — Blocking Bridge Demo
///
/// Demonstrates blocking/thread API (RabbitMQ, gRPC client) and foreign
/// awaitables without exposing a sender API.
///
/// Implementation note.
/// 1. blocking_invoke - blockingthread, blocking io_context
/// 2. from_awaitable - third-party awaitable

import std;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

using namespace cnetmod;

// =============================================================================
// Simulatethird-partyblocking API( RabbitMQ C Client)
// =============================================================================

namespace fake_rabbitmq {

/// Simulateblocking(realblockingthreadwait for I/O)
auto consume(std::string_view queue, int timeout_ms) -> std::string
{
    // Simulateblockingwait for
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return std::format("[{}] message: hello from {}", queue,
        std::this_thread::get_id());
}

/// Simulateblocking
void publish(std::string_view exchange, std::string_view msg)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::println("  [rabbitmq] published to '{}': {} (thread {})",
        exchange, msg, std::this_thread::get_id());
}

} // namespace fake_rabbitmq

// =============================================================================
// Simulateblockingquery
// =============================================================================

namespace fake_db {

struct row
{
    int id;
    std::string name;
};

auto query([[maybe_unused]] std::string_view sql) -> std::vector<row>
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return {{1, "Alice"}, {2, "Bob"}, {3, "Charlie"}};
}

} // namespace fake_db

// =============================================================================
// Simulatethird-party awaitable
// =============================================================================

/// Simple "" awaitable -- simulatethird-partyreturn
struct foreign_awaitable
{
    int value_;

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
        // Simulateasync: threadcomplete
        std::jthread([h]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                h.resume();
            })
            .detach();
    }

    auto await_resume() const noexcept -> int
    {
        return value_;
    }
};

// =============================================================================
// Demo 1: blocking_invoke - blocking API bridge
// =============================================================================

auto demo_blocking_invoke(server_context& ctx) -> task<void>
{
    std::println("\n=== Demo 1: blocking_invoke ===");
    std::println("  io_context thread: {}", std::this_thread::get_id());

    auto& pool = ctx.pool();
    auto& io = ctx.accept_io();

    // 1a. RabbitMQ (blockingthread)
    auto msg = co_await blocking_invoke(pool, io, []
        {
            return fake_rabbitmq::consume("orders", 200);
        });
    std::println("  received: {} (back on thread {})",
        msg, std::this_thread::get_id());

    // 1b. (void return)
    co_await blocking_invoke(pool, io, []
        {
            fake_rabbitmq::publish("notifications", "order_created");
        });
    std::println("  publish done (back on thread {})",
        std::this_thread::get_id());

    // 1c. query
    auto rows = co_await blocking_invoke(pool, io, []
        {
            return fake_db::query("SELECT * FROM users");
        });
    std::println("  db query returned {} rows:", rows.size());
    for (auto& r : rows)
        std::println("    id={}, name={}", r.id, r.name);
}

// =============================================================================
// Demo 2: blocking_invoke + when_all - concurrentblocking
// =============================================================================

auto demo_concurrent_blocking(server_context& ctx) -> task<void>
{
    std::println("\n=== Demo 2: concurrent blocking_invoke (when_all) ===");

    auto& pool = ctx.pool();
    auto& io = ctx.accept_io();

    auto start = std::chrono::steady_clock::now();

    // Blockingconcurrent: ~ max(200, 100) 200+100
    auto [msg, rows] = co_await when_all(
        blocking_invoke(pool, io, []
            {
                return fake_rabbitmq::consume("events", 200);
            }),
        blocking_invoke(pool, io, []
            {
                return fake_db::query("SELECT * FROM orders");
            }));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                       .count();

    std::println("  rabbitmq: {}", msg);
    std::println("  db: {} rows", rows.size());
    std::println("  total time: {}ms (concurrent, not sequential!)", elapsed);
}

// =============================================================================
// Demo 3: native task composition
// =============================================================================

auto compute_task(int x) -> task<int>
{
    co_return x* x;
}

auto greet_task() -> task<std::string>
{
    co_return std::string{"hello from task"};
}

auto fire_and_forget() -> task<void>
{
    std::println("  [task] fire-and-forget executed on thread: {}",
        std::this_thread::get_id());
    co_return;
}

auto demo_native_task() -> task<void>
{
    std::println("\n=== Demo 3: native task composition ===");

    auto val = co_await compute_task(7);
    std::println("  task result: {}", val);

    auto msg = co_await greet_task();
    std::println("  task message: {}", msg);

    co_await fire_and_forget();
    std::println("  void task done");
}

// =============================================================================
// Demo 4: from_awaitable - third-party awaitable
// =============================================================================

auto demo_from_awaitable() -> task<void>
{
    std::println("\n=== Demo 4: from_awaitable (foreign awaitable) ===");

    // Awaitable cnetmod task<int>
    auto result = co_await from_awaitable<int>(foreign_awaitable{99});
    std::println("  foreign awaitable result: {}", result);

    // Co_await(task<T> promise awaitable)
    auto result2 = co_await foreign_awaitable{77};
    std::println("  direct co_await result: {}", result2);
}

// =============================================================================
// Main
// =============================================================================

auto run_all(server_context& ctx) -> task<void>
{
    co_await demo_blocking_invoke(ctx);
    co_await demo_concurrent_blocking(ctx);
    co_await demo_native_task();
    co_await demo_from_awaitable();

    std::println("\n=== All demos complete ===");
    ctx.stop();
}

auto main() -> int
{
    std::println("=== cnetmod: Blocking Bridge Demo ===");
    std::println("Main thread: {}", std::this_thread::get_id());

    // server_context owns io_context workers and a cnetmod CPU pool.
    server_context ctx(1 /*workers*/, 4 /*pool_threads*/);

    spawn(ctx.accept_io(), run_all(ctx));
    ctx.run(); // Blocking stop()

    return 0;
}
