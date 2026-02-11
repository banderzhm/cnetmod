/// cnetmod example — Async Redis Client (RESP3)
/// 演示 redis::client 的 HELLO 3 / AUTH / 基础命令 / pipeline / request builder / stdexec 桥接
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
using redis_client = cn::redis::client;
using cn::redis::request;
using cn::redis::resp3_node;
using cn::redis::resp3_type;
using cn::redis::first_value;
using cn::redis::all_values;
using cn::redis::is_ok;
using cn::redis::has_error;
using cn::redis::error_message;

/// 打印响应节点列表
void print_nodes(std::string_view label, const std::vector<resp3_node>& nodes) {
    std::print("  {:<20} -> ", label);
    if (nodes.empty()) { std::println("(empty)"); return; }
    auto& first = nodes.front();
    if (first.is_null()) { std::println("(nil)"); return; }
    if (first.is_error()) { std::println("(error) {}", first.value); return; }
    if (first.is_aggregate()) {
        std::print("[{}] ", cn::redis::type_name(first.data_type));
        auto vals = all_values(nodes);
        for (std::size_t i = 0; i < vals.size(); ++i) {
            if (i) std::print(", ");
            std::print("\"{}\"", vals[i]);
        }
        std::println();
    } else {
        std::println("{}", first.value);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1: 基础命令 (使用 cmd initializer_list)
// ─────────────────────────────────────────────────────────────────────────────

auto demo_basic(redis_client& r) -> cn::task<void> {
    std::println("\n── Basic Commands ──");

    auto pong = co_await r.cmd({"PING"});
    print_nodes("PING", *pong);

    (void)co_await r.cmd({"SET", "cn:k1", "hello"});
    auto v1 = co_await r.cmd({"GET", "cn:k1"});
    print_nodes("SET+GET cn:k1", *v1);

    (void)co_await r.cmd({"SET", "cn:cnt", "0"});
    for (int i = 0; i < 5; ++i)
        (void)co_await r.cmd({"INCR", "cn:cnt"});
    auto cnt = co_await r.cmd({"GET", "cn:cnt"});
    print_nodes("INCR x5 cn:cnt", *cnt);

    (void)co_await r.cmd({"HSET", "cn:user", "name", "alice", "score", "100"});
    auto hall = co_await r.cmd({"HGETALL", "cn:user"});
    print_nodes("HGETALL cn:user", *hall);

    (void)co_await r.cmd({"RPUSH", "cn:list", "a", "b", "c"});
    auto lr = co_await r.cmd({"LRANGE", "cn:list", "0", "-1"});
    print_nodes("LRANGE cn:list", *lr);

    (void)co_await r.cmd({"SET", "cn:ttl", "tmp", "EX", "30"});
    auto ttl = co_await r.cmd({"TTL", "cn:ttl"});
    print_nodes("TTL cn:ttl", *ttl);

    (void)co_await r.cmd({"DEL", "cn:k1", "cn:cnt", "cn:user", "cn:list", "cn:ttl"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2: request builder + exec
// ─────────────────────────────────────────────────────────────────────────────

auto demo_request_builder(redis_client& r) -> cn::task<void> {
    std::println("\n── Request Builder ──");

    // 单条命令 via request
    request req;
    req.push("SET", "rb:key", "value123");
    auto set_r = co_await r.exec(req);
    std::println("  SET rb:key      -> {}", is_ok(*set_r) ? "OK" : "FAIL");

    // 多命令 request (pipeline 风格)
    request multi;
    multi.push("SET", "rb:a", "alpha");
    multi.push("SET", "rb:b", "beta");
    multi.push("GET", "rb:a");
    multi.push("GET", "rb:b");
    multi.push("DEL", "rb:a", "rb:b", "rb:key");
    auto multi_r = co_await r.exec(multi);
    std::println("  Multi exec ({} commands, {} nodes)", multi.size(), multi_r->size());
    auto vals = all_values(*multi_r);
    for (auto v : vals)
        std::println("    -> {}", v);
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 3: Pipeline — 多条命令单次往返
// ─────────────────────────────────────────────────────────────────────────────

auto demo_pipeline(redis_client& r) -> cn::task<void> {
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

    auto vals = all_values(*replies);
    for (std::size_t i = 0; i < vals.size(); ++i)
        std::println("  [{}] {}", i, vals[i]);
    std::println("  {} nodes in {} us", replies->size(), us);
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 4: stdexec sender 桥接
// ─────────────────────────────────────────────────────────────────────────────

auto demo_sender(cn::io_context& ctx, redis_client& r) -> cn::task<void> {
    std::println("\n── stdexec Sender ──");

    [[maybe_unused]] auto sched = cn::io_scheduler(ctx);
    std::println("  io_scheduler ready (schedule() -> sender)");

    auto square = [](int x) -> cn::task<int> { co_return x * x; };
    auto val = cn::sync_wait_sender(cn::as_sender(square(7)));
    std::println("  sync_wait_sender(square(7)) = {}", val);

    (void)co_await r.cmd({"SET", "s:demo", "via_sender"});
    auto sv = co_await r.cmd({"GET", "s:demo"});
    std::println("  GET s:demo = {}", first_value(*sv));

    (void)co_await r.cmd({"DEL", "s:demo"});
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto run(cn::io_context& ctx) -> cn::task<void> {
    redis_client r(ctx);

    // 连接 Redis（HELLO 3 + AUTH + SELECT）
    auto result = co_await r.connect({
        .host     = "127.0.0.1",
        .port     = 6379,
        .password = "ydc888888",  // 按实际修改，留空则跳过 AUTH
        .username = {},
        .db       = 9,
    });

    if (!result) {
        std::println("Redis 连接失败: {}", result.error());
        ctx.stop();
        co_return;
    }
    std::println("已连接 Redis (RESP3={})", r.is_resp3());

    co_await demo_basic(r);
    co_await demo_request_builder(r);
    co_await demo_pipeline(r);
    co_await demo_sender(ctx, r);

    r.close();
    std::println("\nDone.");
    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Async Redis Client (RESP3) ===");

    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run(*ctx));
    ctx->run();

    return 0;
}