/// cnetmod example — High-level HTTP Server + Client
/// 演示 http::server / http::router / 中间件 / 静态文件 / 文件上传
/// Server 端注册多条路由，Client 端发送多个请求验证

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

namespace cn = cnetmod;
namespace http = cnetmod::http;

constexpr std::uint16_t PORT = 19080;

// =============================================================================
// 客户端：发送一个 HTTP 请求并打印响应
// =============================================================================

auto send_request(cn::io_context& ctx, http::http_method method,
                  std::string_view path, std::string_view body = {})
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
    if (!body.empty()) {
        req.set_body(std::string(body));
    }

    auto req_data = req.serialize();
    auto wr = co_await cn::async_write(ctx, sock,
        cn::const_buffer{req_data.data(), req_data.size()});
    if (!wr) { sock.close(); co_return; }

    // 读响应
    http::response_parser rp;
    std::array<std::byte, 8192> buf{};
    while (!rp.ready()) {
        auto rd = co_await cn::async_read(ctx, sock, cn::buffer(buf));
        if (!rd || *rd == 0) break;
        auto c = rp.consume(reinterpret_cast<const char*>(buf.data()), *rd);
        if (!c) break;
    }

    if (rp.ready()) {
        std::println("    {} {} → {} {}",
                     http::method_to_string(method), path,
                     rp.status_code(), rp.status_message());
        if (!rp.body().empty())
            std::println("    Body: {}", rp.body());
    }
    sock.close();
}

// =============================================================================
// 客户端：发送 multipart/form-data 请求
// =============================================================================

auto send_multipart_request(cn::io_context& ctx, std::string_view path,
                            const http::multipart_builder& mp)
    -> cn::task<void>
{
    auto sock_r = cn::socket::create(cn::address_family::ipv4,
                                     cn::socket_type::stream);
    if (!sock_r) co_return;
    auto sock = std::move(*sock_r);

    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    auto cr = co_await cn::async_connect(ctx, sock, ep);
    if (!cr) { std::println("    connect failed"); co_return; }

    auto body = mp.build();

    http::request req(http::http_method::POST, path);
    req.set_header("Host", std::format("127.0.0.1:{}", PORT));
    req.set_header("Connection", "close");
    req.set_header("Content-Type", mp.content_type());
    req.set_body(std::move(body));

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
        std::println("    POST {} \u2192 {} {}", path,
                     rp.status_code(), rp.status_message());
        if (!rp.body().empty())
            std::println("    Body: {}", rp.body());
    }
    sock.close();
}

// =============================================================================
// 客户端协程：依次发送多个请求
// =============================================================================

auto run_client(cn::io_context& ctx, http::server& srv) -> cn::task<void> {
    // 等待服务端启动
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});

    std::println("\n--- Client: Testing routes ---");

    // 1. GET /
    std::println("  [1] GET /");
    co_await send_request(ctx, http::http_method::GET, "/");

    // 2. GET /api/users/42
    std::println("  [2] GET /api/users/42");
    co_await send_request(ctx, http::http_method::GET, "/api/users/42");

    // 3. GET /api/users/7/posts/99
    std::println("  [3] GET /api/users/7/posts/99");
    co_await send_request(ctx, http::http_method::GET, "/api/users/7/posts/99");

    // 4. POST /api/echo  (带 body)
    std::println("  [4] POST /api/echo");
    co_await send_request(ctx, http::http_method::POST, "/api/echo",
                          "Hello cnetmod!");

    // 5. GET /unknown (404)
    std::println("  [5] GET /unknown");
    co_await send_request(ctx, http::http_method::GET, "/unknown");

    // 6. POST /upload?name=test.txt (raw body)
    std::println("  [6] POST /upload?name=test.txt (raw body)");
    co_await send_request(ctx, http::http_method::POST,
                          "/upload?name=test.txt", "file content here");

    // 7. POST /upload (multipart/form-data: 2 字段 + 2 文件)
    std::println("  [7] POST /upload (multipart/form-data)");
    {
        http::multipart_builder mp;
        mp.add_field("title", "My Upload")
          .add_field("description", "Testing multipart form-data")
          .add_file("doc", "readme.txt", "text/plain",
                    "This is the readme content.")
          .add_file("image", "logo.png", "image/png",
                    "\x89PNG\r\n\x1a\n fake png data");
        co_await send_multipart_request(ctx, "/upload", mp);
    }

    std::println("\n--- Client: All requests done ---");

    srv.stop();
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::println("=== cnetmod: High-level HTTP Demo ===");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // 构建路由
    http::router router;

    // GET / — 欢迎页
    router.get("/", [](http::request_context& ctx) -> cn::task<void> {
        ctx.html(http::status::ok,
            "<h1>Welcome to cnetmod HTTP Server!</h1>"
            "<p>Routes: /api/users/:id, /api/users/:id/posts/:pid, "
            "/api/echo, /static/*filepath</p>");
        co_return;
    });

    // GET /api/users/:id — 用户信息 (JSON)
    router.get("/api/users/:id", [](http::request_context& ctx) -> cn::task<void> {
        auto id = ctx.param("id");
        ctx.json(http::status::ok,
            std::format(R"({{"id":{},"name":"User_{}"}})", id, id));
        co_return;
    });

    // GET /api/users/:id/posts/:pid — 多参数路由
    router.get("/api/users/:id/posts/:pid",
        [](http::request_context& ctx) -> cn::task<void> {
            auto uid = ctx.param("id");
            auto pid = ctx.param("pid");
            ctx.json(http::status::ok,
                std::format(R"({{"user_id":{},"post_id":{},"title":"Post {}"}})",
                            uid, pid, pid));
            co_return;
        });

    // POST /api/echo — 回传请求 body
    router.post("/api/echo", [](http::request_context& ctx) -> cn::task<void> {
        ctx.text(http::status::ok,
            std::format("Echo: {}", ctx.body()));
        co_return;
    });

    // POST /upload — 文件上传
    router.post("/upload", http::save_upload({
        .save_dir = "uploads",
        .default_filename = "upload.bin",
    }));

    // 构建服务器
    http::server srv(*ctx);
    auto listen_r = srv.listen("127.0.0.1", PORT);
    if (!listen_r) {
        std::println("Listen failed: {}", listen_r.error().message());
        return 1;
    }

    // 注册中间件（洋葱模型：recover → access_log → cors → request_id → body_limit → handler）
    srv.use(cn::recover());
    srv.use(cn::access_log());
    srv.use(cn::cors());
    srv.use(cn::request_id());
    srv.use(cn::body_limit(2 * 1024 * 1024));  // 2MB
    srv.set_router(std::move(router));

    std::println("  Server listening on 127.0.0.1:{}", PORT);

    // 启动服务器和客户端
    cn::spawn(*ctx, srv.run());
    cn::spawn(*ctx, run_client(*ctx, srv));

    ctx->run();
    std::println("Done.");
    return 0;
}
