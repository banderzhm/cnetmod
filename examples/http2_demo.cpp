/// cnetmod example — HTTP/2 Server Demo
/// 演示功能：
///   1. TLS + ALPN 协商自动选择 HTTP/2 或 HTTP/1.1
///   2. HTTP/2 多路复用 — 同一 TLS 连接上并发处理多个请求流
///   3. 路由 + 中间件 — 与 HTTP/1.1 共用同一套 router / middleware
///   4. 协议无关的 request_context — handler 不关心底层是 h1 还是 h2
///
/// 用法:
///   http2_demo [port]                        — 使用内置 test_ssl/ 证书
///   http2_demo <cert.pem> <key.pem> [port]   — 使用自定义证书
///
/// 测试 (HTTP/2 over TLS):
///   curl --http2 -k https://localhost:8443/
///   curl --http2 -k https://localhost:8443/api/info
///   curl --http2 -k -X POST -d 'hello h2' https://localhost:8443/api/echo
///   nghttp -v https://localhost:8443/
///
/// 测试 (HTTP/1.1 fallback):
///   curl --http1.1 -k https://localhost:8443/

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;
import cnetmod.middleware.access_log;
import cnetmod.middleware.recover;
import cnetmod.middleware.cors;
import cnetmod.middleware.request_id;

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_HAS_NGHTTP2)

import cnetmod.core.ssl;

namespace cn = cnetmod;
namespace http = cnetmod::http;

// =============================================================================
// 路由 handler：GET / — 欢迎页，显示协议版本
// =============================================================================

auto handle_index(http::request_context& ctx) -> cn::task<void> {
    ctx.html(http::status::ok,
        "<h1>cnetmod HTTP/2 Demo</h1>"
        "<p>This page was served over HTTP/2 (or HTTP/1.1 as fallback).</p>"
        "<ul>"
        "<li>GET  /           — this page</li>"
        "<li>GET  /api/info   — JSON server info</li>"
        "<li>POST /api/echo   — echo request body</li>"
        "<li>GET  /api/users/:id — user info</li>"
        "<li>GET  /health     — health check</li>"
        "</ul>"
        "<p>Test with: <code>curl --http2 -k https://localhost:PORT/</code></p>");
    co_return;
}

// =============================================================================
// 路由 handler：GET /api/info — 服务器信息
// =============================================================================

auto handle_info(http::request_context& ctx) -> cn::task<void> {
    ctx.json(http::status::ok,
        R"({"server":"cnetmod","features":["http2","tls","alpn","multiplexing"],"protocols":["h2","http/1.1"]})");
    co_return;
}

// =============================================================================
// 路由 handler：POST /api/echo — 回传请求 body
// =============================================================================

auto handle_echo(http::request_context& ctx) -> cn::task<void> {
    auto body = ctx.body();
    auto method = ctx.method();
    ctx.json(http::status::ok,
        std::format(R"({{"method":"{}","body_length":{},"echo":"{}"}})",
                    method, body.size(), body));
    co_return;
}

// =============================================================================
// 路由 handler：GET /api/users/:id — 带路由参数
// =============================================================================

auto handle_user(http::request_context& ctx) -> cn::task<void> {
    auto id = ctx.param("id");
    ctx.json(http::status::ok,
        std::format(R"({{"id":{},"name":"User_{}","protocol":"h2/h1.1"}})", id, id));
    co_return;
}

// =============================================================================
// 路由 handler：GET /health — 健康检查
// =============================================================================

auto handle_health(http::request_context& ctx) -> cn::task<void> {
    ctx.json(http::status::ok, R"({"status":"ok"})");
    co_return;
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char* argv[]) {
    logger::init("http2_demo");

    // 默认使用 test_ssl/ 下的证书（相对于源文件路径）
    auto src_dir = std::filesystem::path(__FILE__).parent_path();
    auto default_cert = (src_dir / "test_ssl" / "cert.pem").string();
    auto default_key  = (src_dir / "test_ssl" / "key.pem").string();

    std::string_view cert_path = default_cert;
    std::string_view key_path  = default_key;
    std::uint16_t port = 8443;

    if (argc >= 3) {
        // http2_demo <cert> <key> [port]
        cert_path = argv[1];
        key_path  = argv[2];
        if (argc > 3)
            port = static_cast<std::uint16_t>(std::atoi(argv[3]));
    } else if (argc == 2) {
        // http2_demo <port>
        port = static_cast<std::uint16_t>(std::atoi(argv[1]));
    }

    logger::info("=== cnetmod: HTTP/2 Demo ===");
    logger::info("TLS + ALPN (h2, http/1.1)");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // --- TLS 上下文 ---
    auto ssl_ctx_r = cn::ssl_context::server();
    if (!ssl_ctx_r) {
        logger::error("ssl_context::server() failed: {}",
                      ssl_ctx_r.error().message());
        return 1;
    }
    auto& ssl_ctx = *ssl_ctx_r;

    if (auto r = ssl_ctx.load_cert_file(cert_path); !r) {
        logger::error("load_cert_file failed: {}", r.error().message());
        return 1;
    }
    if (auto r = ssl_ctx.load_key_file(key_path); !r) {
        logger::error("load_key_file failed: {}", r.error().message());
        return 1;
    }

    // 配置 ALPN：优先 h2，回退 http/1.1
    ssl_ctx.configure_alpn_server({"h2", "http/1.1"});

    // --- 路由 ---
    http::router router;
    router.get("/",             handle_index);
    router.get("/api/info",     handle_info);
    router.post("/api/echo",    handle_echo);
    router.get("/api/users/:id", handle_user);
    router.get("/health",       handle_health);

    // --- 服务器 ---
    http::server srv(*ctx);
    auto listen_r = srv.listen("127.0.0.1", port);
    if (!listen_r) {
        logger::error("listen failed: {}", listen_r.error().message());
        return 1;
    }

    srv.use(cn::recover());
    srv.use(cn::access_log());
    srv.use(cn::cors());
    srv.use(cn::request_id());
    srv.set_router(std::move(router));

    // 绑定 TLS 上下文 → 启用 HTTPS + HTTP/2 via ALPN
    srv.set_ssl_context(ssl_ctx);

    logger::info("Listening on https://127.0.0.1:{}", port);
    logger::info("Test HTTP/2:");
    logger::info("  curl --http2 -k https://localhost:{}/", port);
    logger::info("  curl --http2 -k https://localhost:{}/api/info", port);
    logger::info("  curl --http2 -k -X POST -d 'hello' https://localhost:{}/api/echo", port);
    logger::info("  nghttp -v https://localhost:{}/", port);
    logger::info("Test HTTP/1.1 fallback:");
    logger::info("  curl --http1.1 -k https://localhost:{}/", port);
    logger::info("Press Ctrl+C to stop.");

    cn::spawn(*ctx, srv.run());
    ctx->run();

    logger::info("Done.");
    return 0;
}

#else // !CNETMOD_HAS_SSL || !CNETMOD_HAS_NGHTTP2

int main() {
    logger::init("http2_demo");
    logger::error(
        "HTTP/2 demo requires both SSL and nghttp2 support. "
        "Build with -DCNETMOD_ENABLE_SSL=ON and nghttp2 submodule.");
    return 1;
}

#endif