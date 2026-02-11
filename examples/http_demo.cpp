/// cnetmod example — HTTP Server + Client
/// 演示 http::request/response 解析构建 + 异步 TCP I/O
/// 使用 exec::async_scope + io_scheduler 进行结构化并发
/// 服务端监听端口，返回 "Hello from cnetmod!"
/// 客户端连接后发送 GET 请求，打印响应

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
import cnetmod.coro.cancel;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.executor.scheduler;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;

namespace cn = cnetmod;
namespace http = cnetmod::http;

constexpr std::uint16_t PORT = 18080;

// =============================================================================
// HTTP 服务端：处理单个连接
// =============================================================================

auto handle_http_client(cn::io_context& ctx, cn::socket client)
    -> cn::task<void>
{
    // 读取请求
    http::request_parser parser;
    std::array<std::byte, 4096> buf{};

    while (!parser.ready()) {
        cn::cancel_token rd_token;
        auto rd = co_await cn::with_timeout(ctx, std::chrono::seconds{5},
            cn::async_read(ctx, client, cn::buffer(buf), rd_token), rd_token);
        if (!rd || *rd == 0) { client.close(); co_return; }

        auto consumed = parser.consume(
            reinterpret_cast<const char*>(buf.data()), *rd);
        if (!consumed) {
            std::println(stderr, "  [HTTP Server] parse error: {}",
                         consumed.error().message());
            client.close();
            co_return;
        }
    }

    std::println("  [HTTP Server] {} {} (Host: {})",
                 parser.method(), parser.uri(),
                 parser.get_header("Host"));

    // 构建响应
    http::response resp(http::status::ok);
    resp.set_header("Server", "cnetmod/0.1");
    resp.set_header("Content-Type", "text/plain; charset=utf-8");
    resp.set_header("Connection", "close");
    resp.set_body(std::string_view{"Hello from cnetmod!\n"});

    auto data = resp.serialize();
    auto wr = co_await cn::async_write(ctx, client,
        cn::const_buffer{data.data(), data.size()});
    if (!wr)
        std::println(stderr, "  [HTTP Server] write error");

    client.close();
}

// =============================================================================
// HTTP 服务端：accept 循环（处理 1 个请求后停止）
// =============================================================================

auto http_server(cn::io_context& ctx, cn::tcp::acceptor& acc,
                 std::atomic<bool>& server_ready, std::atomic<int>& done)
    -> cn::task<void>
{
    server_ready.store(true);
    std::println("  [HTTP Server] Listening on port {}", PORT);

    cn::cancel_token accept_token;
    auto r = co_await cn::with_timeout(ctx, std::chrono::seconds{10},
        cn::async_accept(ctx, acc.native_socket(), accept_token), accept_token);
    if (!r) {
        std::println(stderr, "  [HTTP Server] accept error/timeout");
        done.fetch_add(1);
        co_return;
    }

    co_await handle_http_client(ctx, std::move(*r));
    done.fetch_add(1);
}

// =============================================================================
// HTTP 客户端
// =============================================================================

auto http_client(cn::io_context& ctx, std::atomic<bool>& server_ready,
                 std::atomic<int>& done)
    -> cn::task<void>
{
    // 等待服务端就绪
    while (!server_ready.load()) {
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{1});
    }

    // TCP 连接
    auto sock_r = cn::socket::create(cn::address_family::ipv4,
                                     cn::socket_type::stream);
    if (!sock_r) { done.fetch_add(1); co_return; }
    auto sock = std::move(*sock_r);

    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    cn::cancel_token conn_token;
    auto cr = co_await cn::with_timeout(ctx, std::chrono::seconds{5},
        cn::async_connect(ctx, sock, ep, conn_token), conn_token);
    if (!cr) {
        std::println(stderr, "  [HTTP Client] connect failed/timeout: {}",
                     cr.error().message());
        done.fetch_add(1);
        co_return;
    }

    // 构建并发送 HTTP 请求
    http::request req(http::http_method::GET, "/hello");
    req.set_header("Host", std::format("127.0.0.1:{}", PORT));
    req.set_header("User-Agent", "cnetmod-example/0.1");
    req.set_header("Connection", "close");

    auto req_data = req.serialize();
    std::println("  [HTTP Client] Sending request:\n{}", req_data);

    auto wr = co_await cn::async_write(ctx, sock,
        cn::const_buffer{req_data.data(), req_data.size()});
    if (!wr) { sock.close(); done.fetch_add(1); co_return; }

    // 读取并解析响应
    http::response_parser resp_parser;
    std::array<std::byte, 4096> buf{};

    while (!resp_parser.ready()) {
        cn::cancel_token rd_token;
        auto rd = co_await cn::with_timeout(ctx, std::chrono::seconds{5},
            cn::async_read(ctx, sock, cn::buffer(buf), rd_token), rd_token);
        if (!rd || *rd == 0) break;

        auto consumed = resp_parser.consume(
            reinterpret_cast<const char*>(buf.data()), *rd);
        if (!consumed) break;
    }

    if (resp_parser.ready()) {
        std::println("  [HTTP Client] Response: {} {}",
                     resp_parser.status_code(), resp_parser.status_message());
        for (auto& [k, v] : resp_parser.headers())
            std::println("    {}: {}", k, v);
        std::println("  [HTTP Client] Body: {}", resp_parser.body());
    }

    sock.close();
    done.fetch_add(1);
}

// =============================================================================
// 主协程：编排 server + client
// =============================================================================

auto run_http_demo(cn::io_context& ctx) -> cn::task<void> {
    cn::tcp::acceptor acc(ctx);
    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    if (auto r = acc.open(ep, {.reuse_address = true}); !r) {
        std::println(stderr, "  Acceptor open failed: {}", r.error().message());
        ctx.stop();
        co_return;
    }

    // exec::async_scope — 结构化并发：管理 server/client 生命周期
    exec::async_scope scope;
    cn::io_scheduler sch(ctx);

    std::atomic<bool> server_ready{false};
    std::atomic<int>  done{0};

    // 通过 async_scope.spawn + starts_on + as_sender 桥接协程到 stdexec
    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(http_server(ctx, acc, server_ready, done))));

    scope.spawn(
        stdexec::starts_on(sch,
            cn::as_sender(http_client(ctx, server_ready, done))));

    // 等待 server + client 都完成
    while (done.load() < 2) {
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{10});
    }

    acc.close();
    std::println("  HTTP demo done.");
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::println("=== cnetmod: HTTP Server + Client Demo ===");

    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_http_demo(*ctx));
    ctx->run();

    std::println("Done.");
    return 0;
}
