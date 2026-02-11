/// cnetmod example — WebSocket Server + Client
/// 演示 ws::connection + exec::async_scope 结构化并发
/// 服务端 accept 后与客户端进行多轮消息交换
/// async_scope.spawn() 管理子任务，scope.on_empty() 等待全部完成

#include <cnetmod/config.hpp>
#include <new>
#include <exec/async_scope.hpp>
#include <stdexec/execution.hpp>

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
import cnetmod.executor.scheduler;
import cnetmod.protocol.tcp;
import cnetmod.protocol.websocket;

namespace cn = cnetmod;
namespace ws = cnetmod::ws;

constexpr std::uint16_t PORT = 18081;

// =============================================================================
// WebSocket 服务端：处理单个连接
// =============================================================================

auto ws_session(cn::io_context& ctx, cn::socket client_sock,
                std::atomic<int>& done) -> cn::task<void> {
    ws::connection conn(ctx);

    // WebSocket 握手
    auto r = co_await conn.async_accept(std::move(client_sock));
    if (!r) {
        std::println(stderr, "  [WS Server] handshake failed: {}",
                     r.error().message());
        co_return;
    }
    std::println("  [WS Server] Client connected (WebSocket handshake OK)");

    // Echo 循环：收到消息后回传
    for (int i = 0; i < 3; ++i) {
        auto msg = co_await conn.async_recv();
        if (!msg) {
            std::println(stderr, "  [WS Server] recv error: {}",
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

        // 回传 echo
        auto echo = std::format("[echo] {}", text);
        auto sr = co_await conn.async_send_text(echo);
        if (!sr) break;
    }

    (void)co_await conn.async_close();
    std::println("  [WS Server] Session done");
    done.fetch_add(1);
}

// =============================================================================
// WebSocket 服务端：accept（处理 1 个连接后退出）
// =============================================================================

auto ws_server(cn::io_context& ctx, cn::tcp::acceptor& acc,
               exec::async_scope& scope, std::atomic<int>& done,
               std::atomic<bool>& server_ready) -> cn::task<void>
{
    server_ready.store(true);
    std::println("  [WS Server] Listening on port {}", PORT);

    auto r = co_await cn::async_accept(ctx, acc.native_socket());
    if (!r) {
        std::println(stderr, "  [WS Server] accept error");
        done.fetch_add(1);
        co_return;
    }

    // 用 async_scope.spawn 管理 session 子任务生命周期
    cn::io_scheduler sch(ctx);
    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(ws_session(ctx, std::move(*r), done))));
}

// =============================================================================
// WebSocket 客户端
// =============================================================================

auto ws_client(cn::io_context& ctx, std::atomic<bool>& server_ready,
               std::atomic<int>& done) -> cn::task<void>
{
    // 等待服务端就绪
    while (!server_ready.load()) {
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{1});
    }

    ws::connection conn(ctx);

    auto url = std::format("ws://127.0.0.1:{}/chat", PORT);
    auto cr = co_await conn.async_connect(url);
    if (!cr) {
        std::println(stderr, "  [WS Client] connect failed: {}",
                     cr.error().message());
        co_return;
    }
    std::println("  [WS Client] Connected to {}", url);

    // 发送 3 条消息
    for (int i = 1; i <= 3; ++i) {
        auto text = std::format("Hello #{} from cnetmod", i);
        auto sr = co_await conn.async_send_text(text);
        if (!sr) break;
        std::println("  [WS Client] sent: {}", text);

        auto msg = co_await conn.async_recv();
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
// 主协程：使用 async_scope 编排全部任务
// =============================================================================

auto run_ws_demo(cn::io_context& ctx) -> cn::task<void> {
    cn::tcp::acceptor acc(ctx);
    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    if (auto r = acc.open(ep); !r) {
        std::println(stderr, "  Acceptor open failed: {}", r.error().message());
        ctx.stop();
        co_return;
    }

    // exec::async_scope — 结构化并发
    // server_task, session_task, client_task 三个子任务全部由 scope 管理
    exec::async_scope scope;
    cn::io_scheduler sch(ctx);
    std::atomic<bool> server_ready{false};
    std::atomic<int>  done{0}; // session + client = 2

    // scope.spawn + starts_on + as_sender：协程 → sender → scope 生命周期管理
    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(ws_server(ctx, acc, scope, done, server_ready))));

    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(ws_client(ctx, server_ready, done))));

    // 等待 session + client 完成 (server 本身只 accept 后就结束)
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
