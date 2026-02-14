/// cnetmod example — Multi-core HTTP Server
/// 演示功能：
///   1. server_context 多核架构（accept 线程 + N 个 worker 线程）
///   2. 连接自动 round-robin 分发到 worker io_context
///   3. pool_post_awaitable 将 CPU 密集型工作卸载到 stdexec 线程池
///   4. 完全兼容原有 http::router / middleware

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
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;

namespace cn = cnetmod;
namespace http = cnetmod::http;

constexpr std::uint16_t PORT = 19100;
constexpr unsigned WORKER_THREADS = 4;

// =============================================================================
// 日志中间件 — 打印线程 ID 以验证多核分发
// =============================================================================

auto logger_middleware() -> http::middleware_fn {
    return [](http::request_context& ctx, http::next_fn next) -> cn::task<void> {
        auto tid = std::this_thread::get_id();
        std::println("  [MW:log] {} {} (thread {})",
                     ctx.method(), ctx.uri(), tid);
        co_await next();
        std::println("  [MW:log] -> {} (thread {})",
                     ctx.resp().status_code(), tid);
    };
}

// =============================================================================
// CPU 密集型 handler — 通过 pool_post_awaitable 卸载到线程池
// =============================================================================

/// 模拟 CPU 密集计算
auto compute_fibonacci(int n) -> std::uint64_t {
    if (n <= 1) return static_cast<std::uint64_t>(n);
    std::uint64_t a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        auto c = a + b;
        a = b;
        b = c;
    }
    return b;
}

/// 需要 server_context 的 handler，演示 pool 卸载
auto handle_compute(cn::server_context& sctx)
    -> http::handler_fn
{
    return [&sctx](http::request_context& ctx) -> cn::task<void> {
        auto n_str = ctx.param("n");
        int n = 30;  // default
        if (!n_str.empty()) {
            std::from_chars(n_str.data(), n_str.data() + n_str.size(), n);
        }

        auto io_tid = std::this_thread::get_id();

        // 切换到 stdexec 线程池执行 CPU 密集计算
        co_await cn::pool_post_awaitable{sctx.pool()};

        auto pool_tid = std::this_thread::get_id();
        auto result = compute_fibonacci(n);

        // 切回 io_context（通过 handler 返回后自动在 io 线程写响应）
        co_await cn::post_awaitable{ctx.io_ctx()};

        auto back_tid = std::this_thread::get_id();

        ctx.json(http::status::ok, std::format(
            R"({{"n":{},"fibonacci":{},"io_thread":"{}","pool_thread":"{}","back_thread":"{}"}})",
            n, result, io_tid, pool_tid, back_tid));
        co_return;
    };
}

// =============================================================================
// 客户端：发送请求并打印响应
// =============================================================================

auto send_request(cn::io_context& ctx, http::http_method method,
                  std::string_view path)
    -> cn::task<void>
{
    auto sock_r = cn::socket::create(cn::address_family::ipv4,
                                     cn::socket_type::stream);
    if (!sock_r) co_return;
    auto sock = std::move(*sock_r);

    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    auto cr = co_await cn::async_connect(ctx, sock, ep);
    if (!cr) { std::println("    connect failed"); co_return; }

    http::request req(method, path);
    req.set_header("Host", std::format("127.0.0.1:{}", PORT));
    req.set_header("Connection", "close");

    auto req_data = req.serialize();
    auto wr = co_await cn::async_write(ctx, sock,
        cn::const_buffer{req_data.data(), req_data.size()});
    if (!wr) { sock.close(); co_return; }

    http::response_parser rp;
    std::array<std::byte, 8192> buf{};
    while (!rp.ready()) {
        auto rd = co_await cn::async_read(ctx, sock, cn::buffer(buf));
        if (!rd || *rd == 0) break;
        auto c = rp.consume(reinterpret_cast<const char*>(buf.data()), *rd);
        if (!c) break;
    }

    if (rp.ready()) {
        std::println("    {} {} -> {} {}",
                     http::method_to_string(method), path,
                     rp.status_code(), rp.status_message());
        if (!rp.body().empty())
            std::println("    Body: {}", rp.body());
    }
    sock.close();
}

// =============================================================================
// 客户端协程
// =============================================================================

auto run_client(cn::server_context& sctx, http::server& srv) -> cn::task<void> {
    auto& ctx = sctx.accept_io();
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{100});

    std::println("\n========== Client: Testing Multi-core HTTP ==========\n");

    // 1. 简单路由 — 不同请求分发到不同 worker 线程
    std::println("  [1] GET / (should go to worker thread)");
    co_await send_request(ctx, http::http_method::GET, "/");

    std::println("\n  [2] GET /api/users/42 (different worker thread)");
    co_await send_request(ctx, http::http_method::GET, "/api/users/42");

    std::println("\n  [3] GET /api/users/7 (another worker)");
    co_await send_request(ctx, http::http_method::GET, "/api/users/7");

    // 2. CPU 密集计算 — 卸载到 stdexec pool
    std::println("\n  [4] GET /compute/35 (CPU work on pool thread)");
    co_await send_request(ctx, http::http_method::GET, "/compute/35");

    std::println("\n  [5] GET /compute/40 (CPU work on pool thread)");
    co_await send_request(ctx, http::http_method::GET, "/compute/40");

    // 3. 并发请求 — 验证多核并行处理
    std::println("\n  [6-9] 4 concurrent requests (should use different workers)");
    // 发 4 个快速请求
    for (int i = 0; i < 4; ++i) {
        std::println("\n    --- request #{} ---", i + 1);
        co_await send_request(ctx, http::http_method::GET,
            std::format("/api/users/{}", i + 100));
    }

    std::println("\n========== Client: All Tests Done ==========\n");

    srv.stop();
    sctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::println("=== cnetmod: Multi-core HTTP Server Demo ===");
    std::println("  Workers: {}, Pool threads: {}\n",
                 WORKER_THREADS, WORKER_THREADS);

    cn::net_init net;

    // 创建多核服务器上下文
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 构建路由
    http::router router;

    // GET / — 欢迎页
    router.get("/", [](http::request_context& ctx) -> cn::task<void> {
        ctx.json(http::status::ok, std::format(
            R"({{"message":"Hello from multi-core cnetmod!","thread":"{}"}})",
            std::this_thread::get_id()));
        co_return;
    });

    // GET /api/users/:id
    router.get("/api/users/:id", [](http::request_context& ctx) -> cn::task<void> {
        auto id = ctx.param("id");
        ctx.json(http::status::ok, std::format(
            R"({{"id":{},"name":"User_{}","thread":"{}"}})",
            id, id, std::this_thread::get_id()));
        co_return;
    });

    // GET /compute/:n — CPU 密集计算（卸载到 pool）
    router.get("/compute/:n", handle_compute(sctx));

    // 构建多核 HTTP 服务器
    http::server srv(sctx);
    auto listen_r = srv.listen("0.0.0.0", PORT);
    if (!listen_r) {
        std::println("Listen failed: {}", listen_r.error().message());
        return 1;
    }

    srv.use(logger_middleware());
    srv.set_router(std::move(router));

    std::println("  Server listening on 0.0.0.0:{}", PORT);
    std::println("  Accept thread: {}", std::this_thread::get_id());

    // 启动服务器和客户端
    cn::spawn(sctx.accept_io(), srv.run());
    cn::spawn(sctx.accept_io(), run_client(sctx, srv));

    // 运行（阻塞：当前线程 accept_io + worker 线程）
    sctx.run();
    std::println("Done.");
    return 0;
}
