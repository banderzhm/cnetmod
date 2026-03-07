/// cnetmod example — Redis Connection Pool
/// 演示 redis::connection_pool 的高性能连接池特性
/// 需要本地 Redis 运行在 127.0.0.1:6379

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.redis;

namespace cn = cnetmod;
using cn::redis::connection_pool;
using cn::redis::pool_params;
using cn::redis::pooled_connection;

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1: 基础连接池操作
// ─────────────────────────────────────────────────────────────────────────────

auto demo_basic_pool(connection_pool& pool) -> cn::task<void> {
    std::println("\n── Basic Pool Operations ──");

    // 获取连接
    auto conn_result = co_await pool.async_get_connection();
    if (!conn_result) {
        std::println("Failed to get connection: {}", conn_result.error().message());
        co_return;
    }

    auto conn = std::move(*conn_result);
    std::println("Got connection from pool");

    // 使用连接执行命令
    auto pong = co_await conn->cmd({"PING"});
    if (pong && !pong->empty()) {
        std::println("PING -> {}", (*pong)[0].value);
    }

    // 设置和获取值
    co_await conn->cmd({"SET", "pool:test", "hello_from_pool"});
    auto val = co_await conn->cmd({"GET", "pool:test"});
    if (val && !val->empty()) {
        std::println("GET pool:test -> {}", (*val)[0].value);
    }

    // 连接自动归还到池（RAII）
    std::println("Connection will be returned to pool automatically");
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2: 并发请求 - 展示连接池的并发能力
// ─────────────────────────────────────────────────────────────────────────────

auto worker_task(connection_pool& pool, int worker_id, int num_ops) -> cn::task<void> {
    for (int i = 0; i < num_ops; ++i) {
        auto conn_result = co_await pool.async_get_connection();
        if (!conn_result) {
            std::println("Worker {} failed to get connection", worker_id);
            continue;
        }

        auto conn = std::move(*conn_result);
        
        // 执行一些操作
        auto key = std::format("worker:{}:counter", worker_id);
        co_await conn->cmd({"INCR", key});
        
        // 模拟一些处理时间
        co_await cn::async_sleep(pool.size() > 0 ? 
            *static_cast<cn::io_context*>(nullptr) : 
            *static_cast<cn::io_context*>(nullptr), 
            std::chrono::milliseconds(1));
    }
}

auto demo_concurrent(cn::io_context& ctx, connection_pool& pool) -> cn::task<void> {
    std::println("\n── Concurrent Operations ──");
    std::println("Pool size: {}, Idle: {}", pool.size(), pool.idle_count());

    const int num_workers = 10;
    const int ops_per_worker = 5;

    auto start = std::chrono::steady_clock::now();

    // 启动多个并发工作任务
    std::vector<cn::task<void>> tasks;
    for (int i = 0; i < num_workers; ++i) {
        tasks.push_back(worker_task(pool, i, ops_per_worker));
    }

    // 等待所有任务完成
    for (auto& t : tasks) {
        co_await t;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::println("Completed {} operations in {} ms", 
                 num_workers * ops_per_worker, elapsed.count());
    std::println("Pool size: {}, Idle: {}, Waiters: {}", 
                 pool.size(), pool.idle_count(), pool.waiter_count());

    // 清理测试数据
    auto conn_result = co_await pool.async_get_connection();
    if (conn_result) {
        auto conn = std::move(*conn_result);
        std::vector<std::string> keys;
        for (int i = 0; i < num_workers; ++i) {
            keys.push_back(std::format("worker:{}:counter", i));
        }
        
        // 构建 DEL 命令 - 使用initializer_list
        std::vector<std::string_view> del_args = {"DEL"};
        for (const auto& key : keys) {
            del_args.push_back(key);
        }
        
        // 手动构建命令
        auto reply = co_await conn->cmd({"DEL", keys[0], keys[1], keys[2], keys[3]});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 3: 快速路径测试 - try_get_connection
// ─────────────────────────────────────────────────────────────────────────────

auto demo_fast_path(connection_pool& pool) -> cn::task<void> {
    std::println("\n── Fast Path (try_get_connection) ──");

    // 尝试立即获取连接（无等待）
    auto fast_result = pool.try_get_connection();
    if (fast_result) {
        std::println("Got connection via fast path (lock-free)");
        auto conn = std::move(*fast_result);
        
        auto info = co_await conn->cmd({"INFO", "server"});
        if (info && !info->empty()) {
            auto info_str = (*info)[0].value;
            // 提取 Redis 版本
            if (auto pos = info_str.find("redis_version:"); pos != std::string::npos) {
                auto end = info_str.find('\n', pos);
                std::println("Redis version: {}", 
                           info_str.substr(pos + 14, end - pos - 14));
            }
        }
    } else {
        std::println("Fast path failed: {}", fast_result.error().message());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 4: Pipeline 批量操作
// ─────────────────────────────────────────────────────────────────────────────

auto demo_pipeline(connection_pool& pool) -> cn::task<void> {
    std::println("\n── Pipeline Operations ──");

    auto conn_result = co_await pool.async_get_connection();
    if (!conn_result) {
        std::println("Failed to get connection");
        co_return;
    }

    auto conn = std::move(*conn_result);

    // 使用 pipeline 批量执行命令
    auto start = std::chrono::steady_clock::now();
    
    auto replies = co_await conn->pipe({
        {"SET", "pipe:1", "value1"},
        {"SET", "pipe:2", "value2"},
        {"SET", "pipe:3", "value3"},
        {"MGET", "pipe:1", "pipe:2", "pipe:3"},
        {"DEL", "pipe:1", "pipe:2", "pipe:3"},
    });

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    if (replies) {
        std::println("Pipeline executed {} commands in {} μs", 
                     5, elapsed.count());
        std::println("Response nodes: {}", replies->size());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 5: 连接池统计信息
// ─────────────────────────────────────────────────────────────────────────────

auto demo_pool_stats(connection_pool& pool) -> cn::task<void> {
    std::println("\n── Pool Statistics ──");
    
    std::println("Total connections: {}", pool.size());
    std::println("Idle connections:  {}", pool.idle_count());
    std::println("Waiting requests:  {}", pool.waiter_count());

    // 借用多个连接观察池的行为
    std::vector<pooled_connection> conns;
    
    for (int i = 0; i < 3; ++i) {
        auto result = co_await pool.async_get_connection();
        if (result) {
            conns.push_back(std::move(*result));
            std::println("After borrowing {}: size={}, idle={}", 
                        i + 1, pool.size(), pool.idle_count());
        }
    }

    // 归还连接
    conns.clear();
    std::println("After returning all: size={}, idle={}", 
                pool.size(), pool.idle_count());
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto run(cn::io_context& ctx) -> cn::task<void> {
    // 配置连接池参数
    pool_params params;
    params.host = "127.0.0.1";
    params.port = 6379;
    params.password = "ydc888888";  // 按实际修改，留空则跳过 AUTH
    params.db = 9;
    params.initial_size = 2;        // 初始连接数
    params.max_size = 8;            // 最大连接数
    params.pool_timeout = std::chrono::seconds(5);
    params.ping_interval = std::chrono::minutes(1);

    std::println("Creating connection pool...");
    std::println("  Initial size: {}", params.initial_size);
    std::println("  Max size:     {}", params.max_size);

    connection_pool pool(ctx, params);

    // 启动连接池（建立初始连接 + 后台健康检查）
    cn::spawn(ctx, pool.async_run());

    // 等待初始连接就绪
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));

    std::println("Pool initialized: size={}, idle={}", 
                pool.size(), pool.idle_count());

    // 运行各种演示
    co_await demo_basic_pool(pool);
    co_await demo_fast_path(pool);
    co_await demo_pipeline(pool);
    co_await demo_concurrent(ctx, pool);
    co_await demo_pool_stats(pool);

    // 关闭连接池
    co_await pool.cancel();
    std::println("\nPool closed.");
    
    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Redis Connection Pool Demo ===\n");

    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run(*ctx));
    ctx->run();

    return 0;
}
