/// cnetmod example — WebSocket Server + Client
/// Demonstrates ws::connection + exec::async_scope concurrent
/// Server accept Client
/// Async_scope.spawn() task, scope.on_empty() wait forcomplete

#include <cnetmod/config.hpp>
#include <new>
#include <exec/async_scope.hpp>
#include <stdexec/execution.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.websocket;

namespace cn = cnetmod;
namespace ws = cnetmod::ws;

constexpr std::uint16_t PORT = 18081;

// =============================================================================
// WebSocket Server: handle one connection
// =============================================================================

auto ws_session(cn::io_context& ctx, cn::socket client_sock,
                std::atomic<int>& done) -> cn::task<void> {
    ws::connection conn(ctx);

    // WebSocket
    auto r = co_await conn.async_accept(std::move(client_sock));
    if (!r) {
        std::println(stderr, "  [WS Server] handshake failed: {}",
                     r.error().message());
        co_return;
    }
    std::println("  [WS Server] Client connected (WebSocket handshake OK)");

    // Echo : echo back(10)
    for (int i = 0; i < 3; ++i) {
        cn::cancel_token recv_token;
        conn.set_cancel_token(&recv_token);
        auto msg = co_await cn::with_timeout(ctx, std::chrono::seconds{10},
            conn.async_recv(), recv_token);
        conn.clear_cancel_token();
        if (!msg) {
            std::println(stderr, "  [WS Server] recv error/timeout: {}",
                         msg.error().message());
            break;
        }

        if (msg->op == ws::opcode::close) {
            std::println("  [WS Server] Client sent close");
            break;
        }

        std::string_view text(
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size());
        std::println("  [WS Server] recv: {}", text);

        // Echo back echo
        auto echo = std::format("[echo] {}", text);
        auto sr = co_await conn.async_send_text(echo);
        if (!sr) break;
    }

    (void)co_await conn.async_close();
    std::println("  [WS Server] Session done");
    done.fetch_add(1);
}

// =============================================================================
// WebSocket Server: accept( 1 )
// =============================================================================

auto ws_server(cn::io_context& ctx, cn::tcp::acceptor& acc,
               exec::async_scope& scope, std::atomic<int>& done,
               std::atomic<bool>& server_ready) -> cn::task<void>
{
    server_ready.store(true);
    std::println("  [WS Server] Listening on port {}", PORT);

    cn::cancel_token accept_token;
    auto r = co_await cn::with_timeout(ctx, std::chrono::seconds{10},
        cn::async_accept(ctx, acc.native_socket(), accept_token), accept_token);
    if (!r) {
        std::println(stderr, "  [WS Server] accept error/timeout");
        done.fetch_add(1);
        co_return;
    }

    // Async_scope.spawn session tasklifetime
    cn::io_scheduler sch(ctx);
    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(ws_session(ctx, std::move(*r), done))));
}

// =============================================================================
// WebSocket Client
// =============================================================================

auto ws_client(cn::io_context& ctx, std::atomic<bool>& server_ready,
               std::atomic<int>& done) -> cn::task<void>
{
    // Wait forServer
    while (!server_ready.load()) {
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{1});
    }

    ws::connection conn(ctx);

    auto url = std::format("ws://*********:{}/chat", PORT);
    cn::cancel_token conn_token;
    conn.set_cancel_token(&conn_token);
    auto cr = co_await cn::with_timeout(ctx, std::chrono::seconds{5},
        conn.async_connect(url), conn_token);
    conn.clear_cancel_token();
    if (!cr) {
        std::println(stderr, "  [WS Client] connect failed/timeout: {}",
                     cr.error().message());
        co_return;
    }
    std::println("  [WS Client] Connected to {}", url);

    // Implementation note: 3 .
    for (int i = 1; i <= 3; ++i) {
        auto text = std::format("Hello #{} from cnetmod", i);
        auto sr = co_await conn.async_send_text(text);
        if (!sr) break;
        std::println("  [WS Client] sent: {}", text);

        cn::cancel_token recv_token;
        conn.set_cancel_token(&recv_token);
        auto msg = co_await cn::with_timeout(ctx, std::chrono::seconds{10},
            conn.async_recv(), recv_token);
        conn.clear_cancel_token();
        if (!msg) break;

        if (msg->op == ws::opcode::close) break;

        std::string_view reply(
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size());
        std::println("  [WS Client] recv: {}", reply);
    }

    (void)co_await conn.async_close();
    std::println("  [WS Client] Done");
    done.fetch_add(1);
}

// =============================================================================
// Main coroutine: async_scope task
// =============================================================================

auto run_ws_demo(cn::io_context& ctx) -> cn::task<void> {
    cn::tcp::acceptor acc(ctx);
    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    if (auto r = acc.open(ep, {.reuse_address = true}); !r) {
        std::println(stderr, "  Acceptor open failed: {}", r.error().message());
        ctx.stop();
        co_return;
    }

    // Exec::async_scope - concurrent
    // Server_task, session_task, client_task task scope
    exec::async_scope scope;
    cn::io_scheduler sch(ctx);
    std::atomic<bool> server_ready{false};
    std::atomic<int>  done{0}; // session + client = 2

    // Scope.spawn + starts_on + as_sender: -> sender -> scope lifetime
    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(ws_server(ctx, acc, scope, done, server_ready))));

    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(ws_client(ctx, server_ready, done))));

    // Wait for session + client complete (server accept )
    while (done.load() < 2) {
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});
    }

    acc.close();
    std::println("  WebSocket demo done.");
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::println("=== cnetmod: WebSocket Server + Client Demo ===");

    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_ws_demo(*ctx));
    ctx->run();

    std::println("Done.");
    return 0;
}
