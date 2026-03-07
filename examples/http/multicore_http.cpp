/// cnetmod example — Multi-core HTTP Server
/// 演示功能：
///   1. server_context 多核架构（accept 线程 + N 个 worker 线程）
///   2. 连接自动 round-robin 分发到 worker io_context
///   3. pool_post_awaitable 将 CPU 密集型工作卸载到 stdexec 线程池
///   4. 完全兼容原有 http::router / middleware

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;
import cnetmod.middleware.access_log;
import cnetmod.middleware.recover;
import cnetmod.middleware.cors;
import cnetmod.middleware.request_id;
import cnetmod.middleware.body_limit;
import cnetmod.middleware.rate_limiter;
import cnetmod.middleware.jwt_auth;

namespace cn = cnetmod;
namespace http = cnetmod::http;

constexpr std::uint16_t PORT = 19100;
constexpr unsigned WORKER_THREADS = 4;

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
                  std::string_view path,
                  std::vector<std::pair<std::string, std::string>> extra_headers = {},
                  std::string_view body = {})
    -> cn::task<std::string>
{
    auto sock_r = cn::socket::create(cn::address_family::ipv4,
                                     cn::socket_type::stream);
    if (!sock_r) co_return "";
    auto sock = std::move(*sock_r);

    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    auto cr = co_await cn::async_connect(ctx, sock, ep);
    if (!cr) { std::println("    connect failed"); co_return ""; }

    http::request req(method, path);
    req.set_header("Host", std::format("*********:{}", PORT));
    req.set_header("Connection", "close");
    for (auto& [k, v] : extra_headers)
        req.set_header(k, v);
    if (!body.empty())
        req.set_body(std::string(body));

    auto req_data = req.serialize();
    auto wr = co_await cn::async_write(ctx, sock,
        cn::const_buffer{req_data.data(), req_data.size()});
    if (!wr) { sock.close(); co_return ""; }

    http::response_parser rp;
    std::array<std::byte, 8192> buf{};
    while (!rp.ready()) {
        auto rd = co_await cn::async_read(ctx, sock, cn::buffer(buf));
        if (!rd || *rd == 0) break;
        auto c = rp.consume(reinterpret_cast<const char*>(buf.data()), *rd);
        if (!c) break;
    }

    std::string resp_body;
    if (rp.ready()) {
        std::println("    {} {} -> {} {}",
                     http::method_to_string(method), path,
                     rp.status_code(), rp.status_message());
        resp_body = rp.body();
        if (!resp_body.empty())
            std::println("    Body: {}", resp_body);
    }
    sock.close();
    co_return resp_body;
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

    // 4. JWT 认证测试
    std::println("\n  [10] GET /api/secret (no auth → 401)");
    co_await send_request(ctx, http::http_method::GET, "/api/secret");

    std::println("\n  [11] GET /api/secret (valid token → 200)");
    co_await send_request(ctx, http::http_method::GET, "/api/secret",
        {{"Authorization", "Bearer demo-secret"}});

    std::println("\n  [12] GET /api/secret (bad token → 401)");
    co_await send_request(ctx, http::http_method::GET, "/api/secret",
        {{"Authorization", "Bearer wrong-token"}});

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

    // GET /api/secret — JWT 保护路由
    router.get("/api/secret", [](http::request_context& ctx) -> cn::task<void> {
        ctx.json(http::status::ok,
            R"({"data":"top secret payload","access":"authorized"})");
        co_return;
    });

    // 构建多核 HTTP 服务器
    http::server srv(sctx);
    auto listen_r = srv.listen("0.0.0.0", PORT);
    if (!listen_r) {
        std::println("Listen failed: {}", listen_r.error().message());
        return 1;
    }

    // 注册中间件（洋葱模型：recover → access_log → cors → request_id → body_limit → rate_limiter → jwt_auth → handler）
    srv.use(cn::recover());
    srv.use(cn::access_log());
    srv.use(cn::cors());
    srv.use(cn::request_id());
    srv.use(cn::body_limit(2 * 1024 * 1024));  // 2MB
    srv.use(cn::rate_limiter({.rate = 100.0, .burst = 200.0}));  // 宽松限流
    srv.use(cn::jwt_auth({
        .verify = [](std::string_view token) { return token == "demo-secret"; },
        .skip_paths = {"/", "/api/users", "/compute"},
    }));
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
