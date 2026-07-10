/// cnetmod example — Redis Sharded Connection Pool
/// Demonstrates redis::sharded_connection_pool multicore
/// Threadconcurrent
/// Redis run 127.0.0.1:6379

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.redis;

namespace cn = cnetmod;
using cn::redis::sharded_connection_pool;
using cn::redis::pool_params;
using cn::redis::pooled_connection;

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1: basic
// ─────────────────────────────────────────────────────────────────────────────

auto demo_basic_sharded(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Basic Sharded Pool Operations ──");
    std::println("Shard count: {}", pool.shard_count());
    std::println("Total connections: {}", pool.size());
    std::println("Total idle: {}", pool.idle_count());

    // Implementation note.
    auto conn_result = co_await pool.async_get_connection();
    if (!conn_result) {
        std::println("Failed to get connection: {}", conn_result.error().message());
        co_return;
    }

    auto conn = std::move(*conn_result);
    std::println("Got connection from sharded pool");

    // Implementation note.
    auto pong = co_await conn->cmd({"PING"});
    if (pong && !pong->empty()) {
        std::println("PING -> {}", (*pong)[0].value);
    }

    co_await conn->cmd({"SET", "sharded:test", "hello_from_shard"});
    auto val = co_await conn->cmd({"GET", "sharded:test"});
    if (val && !val->empty()) {
        std::println("GET sharded:test -> {}", (*val)[0].value);
    }

    co_await conn->cmd({"DEL", "sharded:test"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2: concurrentTest
// ─────────────────────────────────────────────────────────────────────────────

auto worker_task(sharded_connection_pool& pool, int worker_id, int num_ops, 
                 std::atomic<int>& success_count) -> cn::task<void> {
    for (int i = 0; i < num_ops; ++i) {
        auto conn_result = co_await pool.async_get_connection();
        if (!conn_result) continue;

        auto conn = std::move(*conn_result);
        
        // Implementation note.
        auto key = std::format("bench:worker:{}:{}", worker_id, i);
        auto set_result = co_await conn->cmd({"SET", key, "value", "EX", "60"});
        if (set_result) {
            success_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

auto demo_high_concurrency(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── High Concurrency Benchmark ──");
    
    const int num_workers = 20;      // 20 concurrentthread
    const int ops_per_worker = 100;  // Thread 100
    const int total_ops = num_workers * ops_per_worker;

    std::atomic<int> success_count{0};
    auto start = std::chrono::steady_clock::now();

    // Starttask
    std::vector<cn::task<void>> tasks;
    for (int i = 0; i < num_workers; ++i) {
        tasks.push_back(worker_task(pool, i, ops_per_worker, success_count));
    }

    // Wait forcomplete
    for (auto& t : tasks) {
        co_await t;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::println("Completed {} operations in {} ms", total_ops, elapsed.count());
    std::println("Success rate: {}/{} ({:.1f}%)", 
                 success_count.load(), total_ops,
                 100.0 * success_count.load() / total_ops);
    std::println("Throughput: {:.0f} ops/sec", 
                 1000.0 * total_ops / elapsed.count());
    std::println("Pool stats: size={}, idle={}", pool.size(), pool.idle_count());
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 3: io_context
// ─────────────────────────────────────────────────────────────────────────────

auto demo_context_binding(cn::io_context& ctx, sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Context-Bound Shard Selection ──");

    // Io_context ()
    auto conn_result = co_await pool.async_get_connection(ctx);
    if (!conn_result) {
        std::println("Failed to get connection");
        co_return;
    }

    auto conn = std::move(*conn_result);
    std::println("Got connection bound to io_context");

    // Implementation note.
    co_await conn->cmd({"SET", "ctx:bound", "value"});
    auto val = co_await conn->cmd({"GET", "ctx:bound"});
    if (val && !val->empty()) {
        std::println("GET ctx:bound -> {}", (*val)[0].value);
    }

    co_await conn->cmd({"DEL", "ctx:bound"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 4: load balancing
// ─────────────────────────────────────────────────────────────────────────────

auto demo_load_balancing(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Load Balancing Across Shards ──");

    // Implementation note.
    std::vector<pooled_connection> conns;
    
    std::println("Borrowing {} connections...", pool.shard_count() * 2);
    for (std::size_t i = 0; i < pool.shard_count() * 2; ++i) {
        auto result = co_await pool.async_get_connection();
        if (result) {
            conns.push_back(std::move(*result));
        }
    }

    std::println("Borrowed {} connections", conns.size());
    std::println("Pool stats: size={}, idle={}", pool.size(), pool.idle_count());

    // Implementation note.
    conns.clear();
    std::println("After returning: size={}, idle={}", pool.size(), pool.idle_count());
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 5: Pipeline batch()
// ─────────────────────────────────────────────────────────────────────────────

auto demo_sharded_pipeline(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Pipeline with Sharded Pool ──");

    auto conn_result = co_await pool.async_get_connection();
    if (!conn_result) {
        std::println("Failed to get connection");
        co_return;
    }

    auto conn = std::move(*conn_result);

    // Batch - initializer_list
    auto start = std::chrono::steady_clock::now();
    auto replies = co_await conn->pipe({
        {"SET", "pipe:0", "value0"},
        {"SET", "pipe:1", "value1"},
        {"SET", "pipe:2", "value2"},
        {"SET", "pipe:3", "value3"},
        {"SET", "pipe:4", "value4"},
        {"SET", "pipe:5", "value5"},
        {"SET", "pipe:6", "value6"},
        {"SET", "pipe:7", "value7"},
        {"SET", "pipe:8", "value8"},
        {"SET", "pipe:9", "value9"},
        {"MGET", "pipe:0", "pipe:1", "pipe:2", "pipe:3", "pipe:4"}
    });
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    if (replies) {
        std::println("Pipeline executed 11 commands in {} μs", elapsed.count());
    }

    // Implementation note.
    co_await conn->cmd({"DEL", "pipe:0", "pipe:1", "pipe:2", "pipe:3", "pipe:4", 
                        "pipe:5", "pipe:6", "pipe:7", "pipe:8", "pipe:9"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 6: io_context (thread)
// ─────────────────────────────────────────────────────────────────────────────

auto worker_on_context(cn::io_context& ctx, sharded_connection_pool& pool, 
                       int worker_id, int num_ops) -> cn::task<void> {
    for (int i = 0; i < num_ops; ++i) {
        // Io_context ()
        auto conn_result = co_await pool.async_get_connection(ctx);
        if (!conn_result) continue;

        auto conn = std::move(*conn_result);
        auto key = std::format("mt:worker:{}:{}", worker_id, i);
        co_await conn->cmd({"SET", key, "value", "EX", "60"});
    }
}

auto demo_multi_context(std::vector<std::unique_ptr<cn::io_context>>& contexts,
                       sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Multi-Context Mode (True Multi-Threading) ──");
    std::println("Running {} worker threads", contexts.size());

    const int ops_per_worker = 50;
    auto start = std::chrono::steady_clock::now();

    // Io_context starttask
    std::vector<std::jthread> threads;
    for (std::size_t i = 0; i < contexts.size(); ++i) {
        threads.emplace_back([&ctx = *contexts[i], &pool, i]() {
            cn::spawn(ctx, worker_on_context(ctx, pool, static_cast<int>(i), 50));
            ctx.run();
        });
    }

    // Wait forthreadcomplete
    threads.clear();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::println("Completed {} operations in {} ms", 
                 contexts.size() * ops_per_worker, elapsed.count());
    std::println("Pool stats: size={}, idle={}", pool.size(), pool.idle_count());

    co_return;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

auto run_single_context(cn::io_context& ctx) -> cn::task<void> {
    std::println("=== Single Context Mode ===\n");

    // Configure
    pool_params params;
    params.host = "127.0.0.1";
    params.port = 6379;
    params.password = "ydc888888";  // Implementation note.
    params.db = 9;
    params.initial_size = 8;        // Implementation note.
    params.max_size = 32;           // Implementation note.
    params.pool_timeout = std::chrono::seconds(5);

    const std::size_t num_shards = 4;  // Implementation note: 4 .
    std::println("Creating sharded pool with {} shards", num_shards);
    std::println("  Total initial size: {}", params.initial_size);
    std::println("  Total max size:     {}", params.max_size);
    std::println("  Per-shard initial:  {}", params.initial_size / num_shards);
    std::println("  Per-shard max:      {}", params.max_size / num_shards);

    sharded_connection_pool pool(ctx, params, num_shards);

    // Startconnection pool
    co_await pool.async_run();
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));

    std::println("\nPool initialized:");
    std::println("  Shards: {}", pool.shard_count());
    std::println("  Total connections: {}", pool.size());
    std::println("  Total idle: {}", pool.idle_count());

    // RunDemonstrates
    co_await demo_basic_sharded(pool);
    co_await demo_context_binding(ctx, pool);
    co_await demo_load_balancing(pool);
    co_await demo_sharded_pipeline(pool);
    co_await demo_high_concurrency(pool);

    // Implementation note.
    co_await pool.cancel();
    std::println("\nPool closed.");
    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Redis Sharded Connection Pool Demo ===\n");

    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_single_context(*ctx));
    ctx->run();

    return 0;
}