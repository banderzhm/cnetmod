/**
 * @brief Entry point for the minimal QUIC echo server (Phase 2 testing).
 *
 * Usage:
 *   ./quic_echo_server_minimal --port 4433 \
 *       --cert server.crt --key server.key
 *
 * 需要预先准备自签名证书（可用 openssl 生成）：
 *   openssl req -x509 -newkey rsa:2048 -nodes \
 *       -keyout server.key -out server.crt \
 *       -days 365 -subj "/CN=localhost"
 */

import std;
import cnetmod.core;
import cnetmod.test.quic_echo_server_minimal;

int main(int argc, char** argv) {
    std::uint16_t port = 4433;
    std::string cert_file = "server.crt";
    std::string key_file  = "server.key";

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoull(argv[++i]));
        } else if (arg == "--cert" && i + 1 < argc) {
            cert_file = argv[++i];
        } else if (arg == "--key" && i + 1 < argc) {
            key_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::println("Usage: {} [options]", argv[0]);
            std::println("Options:");
            std::println("  --port <N>    UDP port to listen on (default: 4433)");
            std::println("  --cert <file> TLS certificate file (default: server.crt)");
            std::println("  --key  <file> TLS private key file (default: server.key)");
            std::println("  --help, -h    Show this help message");
            return 0;
        }
    }

    // 创建 I/O 上下文
    auto ctx = cnetmod::core::make_io_context();

    // 创建 QUIC 服务端 SSL 上下文
    auto ssl_ctx = cnetmod::core::ssl_context::quic_server();

    // 加载证书和私钥
    if (!ssl_ctx.load_cert_file(cert_file)) {
        std::println(stderr,
            "Failed to load certificate: {}", cert_file);
        return 1;
    }

    if (!ssl_ctx.load_key_file(key_file)) {
        std::println(stderr,
            "Failed to load private key: {}", key_file);
        return 1;
    }

    // 创建并启动 minimal echo 服务端
    auto server = cnetmod::test::quic_echo_server_minimal{
        *ctx, ssl_ctx, port
    };

    auto result = server.start().sync_wait();

    if (!result) {
        std::println(stderr,
            "Failed to start server: {}", result.error().message());
        return 1;
    }

    std::println("QUIC echo server (minimal) running on port {}", port);
    std::println("Certificate: {}", cert_file);
    std::println("Private key: {}", key_file);
    std::println("Press Ctrl+C to stop");

    // 运行事件循环（阻塞直到收到 SIGINT）
    ctx->run();

    return 0;
}
