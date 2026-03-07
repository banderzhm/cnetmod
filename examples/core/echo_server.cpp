/// cnetmod example — TCP Echo Server/Client
/// 演示异步 accept / connect / read / write
/// 服务端接受多个连接，每个连接独立回显

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;

namespace cn = cnetmod;

constexpr std::uint16_t PORT = 54321;
constexpr int NUM_CLIENTS = 3;

// =============================================================================
// 服务端：单连接处理协程
// =============================================================================

auto handle_client(cn::io_context& ctx, cn::socket client, int id,
                   std::atomic<int>& done) -> cn::task<void>
{
    std::array<std::byte, 1024> buf{};
    while (true) {
        auto r = co_await cn::async_read(ctx, client, cn::buffer(buf));
        if (!r) break;

        std::size_t n = *r;
        std::string_view sv(reinterpret_cast<const char*>(buf.data()), n);
        if (n>0)
            std::println("  [Server-{}] recv {} bytes: {}", id, n, sv);

        auto w = co_await cn::async_write(ctx, client, cn::const_buffer{buf.data(), n});
        if (!w) break;
    }
    client.close();
    std::println("  [Server-{}] disconnected", id);
    done.fetch_add(1);
}

// =============================================================================
// 服务端：accept 循环
// =============================================================================

auto accept_loop(cn::io_context& ctx, cn::tcp::acceptor& acc,
                 std::atomic<bool>& ready, std::atomic<int>& done,
                 int max_clients) -> cn::task<void>
{
    ready.store(true);
    std::println("  [Server] Listening on port {}", PORT);

    for (int id = 0; id < max_clients; ++id) {
        auto r = co_await cn::async_accept(ctx, acc.native_socket());
        if (!r) { std::println("  [Server] accept error"); break; }

        std::println("  [Server] Client {} connected", id);

        // spawn 火并忘处理协程，生命周期由 detached_task 管理
        cn::spawn(ctx, handle_client(ctx, std::move(*r), id, done));
    }
}

// =============================================================================
// 客户端协程
// =============================================================================

auto run_client(cn::io_context& ctx, std::atomic<bool>& ready,
                std::atomic<int>& done, int id) -> cn::task<void>
{
    // 等待服务端就绪
    while (!ready.load()) {}

    auto sock_r = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
    if (!sock_r) { done.fetch_add(1); co_return; }
    auto sock = std::move(*sock_r);

    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    auto cr = co_await cn::async_connect(ctx, sock, ep);
    if (!cr) {
        std::println("  [Client-{}] connect failed", id);
        done.fetch_add(1);
        co_return;
    }

    // 发送消息
    auto msg = std::format("Hello from client {}", id);
    auto wr = co_await cn::async_write(ctx, sock, cn::buffer(std::string_view{msg}));
    if (wr)
        std::println("  [Client-{}] sent: {}", id, msg);

    co_await cnetmod::async_sleep(ctx, std::chrono::milliseconds{100});

    // 接收回显
    std::array<std::byte, 256> buf{};
    auto rr = co_await cn::async_read(ctx, sock, cn::buffer(buf));
    if (rr) {
        std::string_view reply(reinterpret_cast<const char*>(buf.data()), *rr);
        std::println("  [Client-{}] echo: {}", id, reply);
    }

    sock.close();
    done.fetch_add(1);
}

// =============================================================================
// 主协程
// =============================================================================

auto run_demo(cn::io_context& ctx) -> cn::task<void> {
    cn::tcp::acceptor acc(ctx);
    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    if (auto r = acc.open(ep); !r) {
        std::println("  Acceptor open failed");
        co_return;
    }

    std::atomic<bool> server_ready{false};
    std::atomic<int> done{0};

    // spawn accept 循环
    cn::spawn(ctx, accept_loop(ctx, acc, server_ready, done, NUM_CLIENTS));

    // spawn 多个客户端
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        cn::spawn(ctx, run_client(ctx, server_ready, done, i));
    }

    // 等待所有客户端 + 服务端 handler 完成
    while (done.load() < NUM_CLIENTS * 2) {
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{10});
    }

    acc.close();
    std::println("  Echo demo done ({} connections)", NUM_CLIENTS);
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

auto main() -> int {
    std::println("=== cnetmod: TCP Echo Server/Client ===");

    cn::net_init net;  // RAII: 跨平台网络初始化/清理

    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_demo(*ctx));
    ctx->run();  // 事件循环驱动所有协程，直到 run_demo 调用 ctx.stop()

    std::println("Done.");
    return 0;
}