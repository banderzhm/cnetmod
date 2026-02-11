/// cnetmod example — Async Redis Client
/// 演示 redis::client 的 AUTH / 基础命令 / pipeline / stdexec sender 桥接
/// 需要本地 Redis 运行在 127.0.0.1:6379

#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.executor.scheduler;
import cnetmod.protocol.redis;

namespace cn = cnetmod;
using redis = cn::redis::client;

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1: 基础命令
// ─────────────────────────────────────────────────────────────────────────────

auto demo_basic(redis& r) -> cn::task<void> {
    std::println("\n── Basic Commands ──");

    auto pong = co_await r.cmd({"PING"});
    std::println("  PING            -> {}", pong.to_string());

    co_await r.cmd({"SET", "cn:k1", "hello"});
    auto v1 = co_await r.cmd({"GET", "cn:k1"});
    std::println("  SET+GET cn:k1   -> {}", v1.to_string());

    co_await r.cmd({"SET", "cn:cnt", "0"});
    for (int i = 0; i < 5; ++i)
        co_await r.cmd({"INCR", "cn:cnt"});
    auto cnt = co_await r.cmd({"GET", "cn:cnt"});
    std::println("  INCR x5 cn:cnt  -> {}", cnt.to_string());

    co_await r.cmd({"HSET", "cn:user", "name", "alice", "score", "100"});
    auto hall = co_await r.cmd({"HGETALL", "cn:user"});
    std::println("  HGETALL cn:user -> {}", hall.to_string());

    co_await r.cmd({"RPUSH", "cn:list", "a", "b", "c"});
    auto lr = co_await r.cmd({"LRANGE", "cn:list", "0", "-1"});
    std::println("  LRANGE cn:list  -> {}", lr.to_string());

    co_await r.cmd({"SET", "cn:ttl", "tmp", "EX", "30"});
    auto ttl = co_await r.cmd({"TTL", "cn:ttl"});
    std::println("  TTL cn:ttl      -> {}", ttl.to_string());

    co_await r.cmd({"DEL", "cn:k1", "cn:cnt", "cn:user", "cn:list", "cn:ttl"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2: Pipeline — 多条命令单次往返
// ─────────────────────────────────────────────────────────────────────────────

auto demo_pipeline(redis& r) -> cn::task<void> {
    std::println("\n── Pipeline ──");

    auto t0 = std::chrono::steady_clock::now();

    auto replies = co_await r.pipe({
        {"SET",  "p:a", "alpha"},
        {"SET",  "p:b", "beta"},
        {"SET",  "p:c", "gamma"},
        {"MGET", "p:a", "p:b", "p:c"},
        {"DEL",  "p:a", "p:b", "p:c"},
    });

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - t0).count();

    for (std::size_t i = 0; i < replies.size(); ++i)
        std::println("  [{}] {}", i, replies[i].to_string());
    std::println("  {} commands in {} us", replies.size(), us);
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 3: stdexec sender 桥接
// ─────────────────────────────────────────────────────────────────────────────

auto demo_sender(cn::io_context& ctx, redis& r) -> cn::task<void> {
    std::println("\n── stdexec Sender ──");

    // io_scheduler: 绑定 io_context 的 stdexec scheduler
    auto sched = cn::io_scheduler(ctx);
    std::println("  io_scheduler ready (schedule() -> sender)");

    // as_sender + sync_wait_sender: 纯计算 task
    auto square = [](int x) -> cn::task<int> { co_return x * x; };
    auto val = cn::sync_wait_sender(cn::as_sender(square(7)));
    std::println("  sync_wait_sender(square(7)) = {}", val);

    // I/O task 在事件循环中 co_await（最自然的方式）
    co_await r.cmd({"SET", "s:demo", "via_sender"});
    auto sv = co_await r.cmd({"GET", "s:demo"});
    std::println("  GET s:demo = {}", sv.to_string());

    // 高阶: as_sender 可组合 stdexec::when_all / let_value / on(sched, ...)
    co_await r.cmd({"DEL", "s:demo"});
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto run(cn::io_context& ctx) -> cn::task<void> {
    redis r(ctx);

    // 连接 Redis（带密码认证 + 选择数据库）
    auto result = co_await r.connect({
        .host = "1.94.173.250",
        .port     = 6379,
        .password = "ydc888888",  // 按实际修改，留空则跳过 AUTH
        .db       = 10,
    });

    if (!result.ok()) {
        std::println("Redis 连接失败: {}", result.to_string());
        ctx.stop();
        co_return;
    }
    std::println("已连接 Redis");

    co_await demo_basic(r);
    co_await demo_pipeline(r);
    co_await demo_sender(ctx, r);

    r.close();
    std::println("\nDone.");
    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Async Redis Client ===");

    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run(*ctx));
    ctx->run();

    return 0;
}