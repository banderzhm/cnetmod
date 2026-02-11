/// cnetmod example — High-level WebSocket Server + Client
/// 演示 ws::server 路由端点注册 + 路径参数 + 多端点客户端交互

#include <cnetmod/config.hpp>

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.net_init;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;
import cnetmod.protocol.websocket;

namespace cn = cnetmod;
namespace ws = cnetmod::ws;

constexpr std::uint16_t PORT = 19081;

// =============================================================================
// /echo 端点：收到消息原样回传
// =============================================================================

auto echo_handler(ws::ws_context& ctx) -> cn::task<void> {
    std::println("  [/echo] Client connected");
    while (ctx.is_open()) {
        auto msg = co_await ctx.recv();
        if (!msg) break;
        if (msg->op == ws::opcode::close) break;

        std::string_view text(
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size());
        std::println("  [/echo] recv: {}", text);

        auto sr = co_await ctx.send_text(std::format("[echo] {}", text));
        if (!sr) break;
    }
    std::println("  [/echo] Client disconnected");
}

// =============================================================================
// /chat/:room 端点：读取路径参数 room，带 room 名前缀回传
// =============================================================================

auto chat_handler(ws::ws_context& ctx) -> cn::task<void> {
    auto room = ctx.param("room");
    std::println("  [/chat/{}] Client joined", room);

    while (ctx.is_open()) {
        auto msg = co_await ctx.recv();
        if (!msg) break;
        if (msg->op == ws::opcode::close) break;

        std::string_view text(
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size());
        std::println("  [/chat/{}] recv: {}", room, text);

        auto reply = std::format("[room:{}] {}", room, text);
        auto sr = co_await ctx.send_text(reply);
        if (!sr) break;
    }
    std::println("  [/chat/{}] Client left", room);
}

// =============================================================================
// 客户端：连接到某个 WS 端点，发 N 条消息并打印回复
// =============================================================================

auto ws_client(cn::io_context& ctx, std::string url,
               std::vector<std::string> messages) -> cn::task<void>
{
    ws::connection conn(ctx);

    auto cr = co_await conn.async_connect(url);
    if (!cr) {
        std::println("    [Client] connect {} failed: {}", url,
                     cr.error().message());
        co_return;
    }
    std::println("    [Client] Connected to {}", url);

    for (auto& text : messages) {
        auto sr = co_await conn.async_send_text(text);
        if (!sr) break;
        std::println("    [Client] sent: {}", text);

        auto msg = co_await conn.async_recv();
        if (!msg || msg->op == ws::opcode::close) break;

        std::string_view reply(
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size());
        std::println("    [Client] recv: {}", reply);
    }

    (void)co_await conn.async_close();
    std::println("    [Client] Closed {}", url);
}

// =============================================================================
// 客户端编排：依次测试两个端点
// =============================================================================

auto run_clients(cn::io_context& ctx, ws::server& srv) -> cn::task<void> {
    // 等待服务端启动
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});

    std::println("\n--- Client: Testing /echo ---");
    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/echo", PORT),
        {"Hello", "World", "cnetmod"});

    std::println("\n--- Client: Testing /chat/general ---");
    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/chat/general", PORT),
        {"Hi everyone", "How are you?"});

    std::println("\n--- Client: Testing /chat/dev ---");
    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/chat/dev", PORT),
        {"Bug fix landed", "Ship it!"});

    std::println("\n--- All tests done ---");

    srv.stop();
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::println("=== cnetmod: High-level WebSocket Demo ===");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // 构建 WS 服务器
    ws::server srv(*ctx);
    auto lr = srv.listen("127.0.0.1", PORT);
    if (!lr) {
        std::println("Listen failed: {}", lr.error().message());
        return 1;
    }

    // 注册端点
    srv.on("/echo", echo_handler);
    srv.on("/chat/:room", chat_handler);

    std::println("  WS Server listening on 127.0.0.1:{}", PORT);
    std::println("  Endpoints: /echo, /chat/:room");

    // 启动
    cn::spawn(*ctx, srv.run());
    cn::spawn(*ctx, run_clients(*ctx, srv));

    ctx->run();
    std::println("Done.");
    return 0;
}
