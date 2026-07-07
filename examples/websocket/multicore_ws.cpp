/// cnetmod example — Multi-core WebSocket Server
/// Demonstratesfeatures
/// 1. server_context multicore WS
/// 2. round-robin worker io_context
/// 3. WS worker thread

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.websocket;
import cnetmod.protocol.http.middleware.access_log;

namespace cn = cnetmod;
namespace ws = cnetmod::ws;

constexpr std::uint16_t PORT = 19101;
constexpr unsigned WORKER_THREADS = 4;

// =============================================================================
// /echo : echo back + thread
// =============================================================================

auto echo_handler(ws::ws_context& ctx) -> cn::task<void> {
    auto tid = std::this_thread::get_id();
    std::println("  [/echo] Client connected (thread {})", tid);

    while (ctx.is_open()) {
        auto msg = co_await ctx.recv();
        if (!msg) break;
        if (msg->op == ws::opcode::close) break;

        std::string_view text(
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size());
        std::println("  [/echo] recv: {} (thread {})", text, tid);

        auto reply = std::format("[echo@thread:{}] {}", tid, text);
        auto sr = co_await ctx.send_text(reply);
        if (!sr) break;
    }
    std::println("  [/echo] Client disconnected (thread {})", tid);
}

// =============================================================================
// /chat/:room
// =============================================================================

auto chat_handler(ws::ws_context& ctx) -> cn::task<void> {
    auto room = ctx.param("room");
    auto tid = std::this_thread::get_id();
    std::println("  [/chat/{}] Client joined (thread {})", room, tid);

    while (ctx.is_open()) {
        auto msg = co_await ctx.recv();
        if (!msg) break;
        if (msg->op == ws::opcode::close) break;

        std::string_view text(
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size());
        std::println("  [/chat/{}] recv: {} (thread {})", room, text, tid);

        auto reply = std::format("[room:{}@thread:{}] {}", room, tid, text);
        auto sr = co_await ctx.send_text(reply);
        if (!sr) break;
    }
    std::println("  [/chat/{}] Client left (thread {})", room, tid);
}

// =============================================================================
// Client: WS
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
    std::println("    [Client] Connected to {} (client thread {})",
                 url, std::this_thread::get_id());

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
// Client-side flow.
// =============================================================================

auto run_clients(cn::server_context& sctx, ws::server& srv) -> cn::task<void> {
    auto& ctx = sctx.accept_io();
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{100});

    std::println("\n--- Client: Testing /echo (3 connections to different workers) ---");

    // 3 echo Client - worker thread
    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/echo", PORT),
        {"Hello from client 1", "Multi-core!"});

    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/echo", PORT),
        {"Hello from client 2"});

    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/echo", PORT),
        {"Hello from client 3"});

    std::println("\n--- Client: Testing /chat/general ---");
    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/chat/general", PORT),
        {"Hi everyone", "Multi-core WebSocket!"});

    std::println("\n--- Client: Testing /chat/dev ---");
    co_await ws_client(ctx,
        std::format("ws://127.0.0.1:{}/chat/dev", PORT),
        {"Bug fix landed", "Ship it!"});

    std::println("\n--- All tests done ---");

    srv.stop();
    sctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::println("=== cnetmod: Multi-core WebSocket Server Demo ===");
    std::println("  Workers: {}\n", WORKER_THREADS);

    cn::net_init net;

    // Createmulticore
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // Build WS
    ws::server srv(sctx);
    auto lr = srv.listen("0.0.0.0", PORT);
    if (!lr) {
        std::println("Listen failed: {}", lr.error().message());
        return 1;
    }

    // Register()
    srv.on("/echo", cn::ws_access_log(echo_handler));
    srv.on("/chat/:room", cn::ws_access_log(chat_handler));

    std::println("  WS Server listening on 0.0.0.0:{}", PORT);
    std::println("  Accept thread: {}", std::this_thread::get_id());
    std::println("  Endpoints: /echo, /chat/:room");

    // Start components.
    cn::spawn(sctx.accept_io(), srv.run());
    cn::spawn(sctx.accept_io(), run_clients(sctx, srv));

    sctx.run();
    std::println("Done.");
    return 0;
}
