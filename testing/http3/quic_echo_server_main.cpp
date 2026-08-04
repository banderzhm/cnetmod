/**
 * @brief Entry point for the minimal QUIC echo server.
 *
 * Usage:
 *   ./quic_echo_server --port 4433
 *
 * 需要预先准备自签名证书（server.crt / server.key），
 * 可使用 openssl 生成：
 *   openssl req -x509 -newkey rsa:2048 -nodes \
 *       -keyout server.key -out server.crt \
 *       -days 365 -subj "/CN=localhost"
 */

import std;
import cnetmod.core;
import cnetmod.test.quic_echo_server;

int main(int argc, char** argv) {
    std::uint16_t port = 4433;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoull(argv[++i]));
        }
    }

    // 创建 I/O 上下文和 SSL 上下文
    auto ctx = cnetmod::core::make_io_context();
    auto ssl_ctx = cnetmod::core::ssl_context::quic_server();

    // 加载证书（测试用自签名证书）
    if (!ssl_ctx.load_cert_file("server.crt")) {
        std::println(stderr, "Failed to load certificate");
        return 1;
    }

    if (!ssl_ctx.load_key_file("server.key")) {
        std::println(stderr, "Failed to load private key");
        return 1;
    }

    // 创建并启动 echo 服务端
    auto server = cnetmod::test::quic_echo_server{*ctx, ssl_ctx, port};

    auto result = server.start().sync_wait();

    if (!result) {
        std::println(stderr, "Failed to start server: {}",
                     result.error().message());
        return 1;
    }

    std::println("QUIC echo server running on port {}", port);
    std::println("Press Ctrl+C to stop");

    // 运行事件循环（阻塞直到收到信号）
    ctx->run();

    return 0;
}
