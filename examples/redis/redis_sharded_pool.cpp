/// cnetmod example — Redis Sharded Connection Pool
/// 演示 redis::sharded_connection_pool 的多核优化特性
/// 通过分片减少锁竞争，提升多线程并发性能
/// 需要本地 Redis 运行在 127.0.0.1:6379

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
// Demo 1: 基础分片池操作
// ─────────────────────────────────────────────────────────────────────────────

auto demo_basic_sharded(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Basic Sharded Pool Operations ──");
    std::println("Shard count: {}", pool.shard_count());
    std::println("Total connections: {}", pool.size());
    std::println("Total idle: {}", pool.idle_count());

    // 获取连接（自动选择分片）
    auto conn_result = co_await pool.async_get_connection();
    if (!conn_result) {
        std::println("Failed to get connection: {}", conn_result.error().message());
        co_return;
    }

    auto conn = std::move(*conn_result);
    std::println("Got connection from sharded pool");

    // 执行命令
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
// Demo 2: 高并发压力测试 - 对比分片池的性能优势
// ─────────────────────────────────────────────────────────────────────────────

auto worker_task(sharded_connection_pool& pool, int worker_id, int num_ops, 
                 std::atomic<int>& success_count) -> cn::task<void> {
    for (int i = 0; i < num_ops; ++i) {
        auto conn_result = co_await pool.async_get_connection();
        if (!conn_result) continue;

        auto conn = std::move(*conn_result);
        
        // 执行操作
        auto key = std::format("bench:worker:{}:{}", worker_id, i);
        auto set_result = co_await conn->cmd({"SET", key, "value", "EX", "60"});
        if (set_result) {
            success_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

auto demo_high_concurrency(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── High Concurrency Benchmark ──");
    
    const int num_workers = 20;      // 20 个并发工作线程
    const int ops_per_worker = 100;  // 每个线程 100 次操作
    const int total_ops = num_workers * ops_per_worker;

    std::atomic<int> success_count{0};
    auto start = std::chrono::steady_clock::now();

    // 启动所有工作任务
    std::vector<cn::task<void>> tasks;
    for (int i = 0; i < num_workers; ++i) {
        tasks.push_back(worker_task(pool, i, ops_per_worker, success_count));
    }

    // 等待完成
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
// Demo 3: 绑定 io_context 的分片选择
// ─────────────────────────────────────────────────────────────────────────────

auto demo_context_binding(cn::io_context& ctx, sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Context-Bound Shard Selection ──");

    // 使用特定 io_context 获取连接（会优先选择绑定的分片）
    auto conn_result = co_await pool.async_get_connection(ctx);
    if (!conn_result) {
        std::println("Failed to get connection");
        co_return;
    }

    auto conn = std::move(*conn_result);
    std::println("Got connection bound to io_context");

    // 执行一些操作
    co_await conn->cmd({"SET", "ctx:bound", "value"});
    auto val = co_await conn->cmd({"GET", "ctx:bound"});
    if (val && !val->empty()) {
        std::println("GET ctx:bound -> {}", (*val)[0].value);
    }

    co_await conn->cmd({"DEL", "ctx:bound"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 4: 分片间负载均衡
// ─────────────────────────────────────────────────────────────────────────────

auto demo_load_balancing(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Load Balancing Across Shards ──");

    // 快速借用多个连接，观察分片分布
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

    // 归还连接
    conns.clear();
    std::println("After returning: size={}, idle={}", pool.size(), pool.idle_count());
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 5: Pipeline 批量操作（分片池）
// ─────────────────────────────────────────────────────────────────────────────

auto demo_sharded_pipeline(sharded_connection_pool& pool) -> cn::task<void> {
    std::println("\n── Pipeline with Sharded Pool ──");

    auto conn_result = co_await pool.async_get_connection();
    if (!conn_result) {
        std::println("Failed to get connection");
        co_return;
    }

    auto conn = std::move(*conn_result);

    // 批量操作 - 使用initializer_list
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

    // 清理
    co_await conn->cmd({"DEL", "pipe:0", "pipe:1", "pipe:2", "pipe:3", "pipe:4", 
                        "pipe:5", "pipe:6", "pipe:7", "pipe:8", "pipe:9"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 6: 多 io_context 模式（真正的多线程）
// ─────────────────────────────────────────────────────────────────────────────

auto worker_on_context(cn::io_context& ctx, sharded_connection_pool& pool, 
                       int worker_id, int num_ops) -> cn::task<void> {
    for (int i = 0; i < num_ops; ++i) {
        // 使用当前 io_context 获取连接（亲和性优化）
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

    // 在每个 io_context 上启动工作任务
    std::vector<std::jthread> threads;
    for (std::size_t i = 0; i < contexts.size(); ++i) {
        threads.emplace_back([&ctx = *contexts[i], &pool, i]() {
            cn::spawn(ctx, worker_on_context(ctx, pool, static_cast<int>(i), 50));
            ctx.run();
        });
    }

    // 等待所有线程完成
    threads.clear();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::println("Completed {} operations in {} ms", 
                 contexts.size() * ops_per_worker, elapsed.count());
    std::println("Pool stats: size={}, idle={}", pool.size(), pool.idle_count());

    co_return;
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto run_single_context(cn::io_context& ctx) -> cn::task<void> {
    std::println("=== Single Context Mode ===\n");

    // 配置分片池参数
    pool_params params;
    params.host = "127.0.0.1";
    params.port = 6379;
    params.password = "ydc888888";  // 按实际修改
    params.db = 9;
    params.initial_size = 8;        // 总初始连接数
    params.max_size = 32;           // 总最大连接数
    params.pool_timeout = std::chrono::seconds(5);

    const std::size_t num_shards = 4;  // 4 个分片
    std::println("Creating sharded pool with {} shards", num_shards);
    std::println("  Total initial size: {}", params.initial_size);
    std::println("  Total max size:     {}", params.max_size);
    std::println("  Per-shard initial:  {}", params.initial_size / num_shards);
    std::println("  Per-shard max:      {}", params.max_size / num_shards);

    sharded_connection_pool pool(ctx, params, num_shards);

    // 启动连接池
    co_await pool.async_run();
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));

    std::println("\nPool initialized:");
    std::println("  Shards: {}", pool.shard_count());
    std::println("  Total connections: {}", pool.size());
    std::println("  Total idle: {}", pool.idle_count());

    // 运行演示
    co_await demo_basic_sharded(pool);
    co_await demo_context_binding(ctx, pool);
    co_await demo_load_balancing(pool);
    co_await demo_sharded_pipeline(pool);
    co_await demo_high_concurrency(pool);

    // 关闭
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
