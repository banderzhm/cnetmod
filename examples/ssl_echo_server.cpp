/// ssl_echo_server — TLS echo server 示例
///
/// 用法:
///   ssl_echo_server <cert.pem> <key.pem> [port]
///
/// 测试:
///   openssl s_client -connect 127.0.0.1:8443

#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

using namespace cnetmod;

#ifdef CNETMOD_HAS_SSL

auto handle_client(io_context& ctx, socket client_sock, ssl_context& ssl_ctx)
    -> task<void>
{
    // 创建 SSL 流
    ssl_stream stream(ssl_ctx, ctx, client_sock);
    stream.set_accept_state();

    // TLS 握手
    auto hs = co_await stream.async_handshake();
    if (!hs) {
        std::println(stderr, "TLS handshake failed: {}", hs.error().message());
        client_sock.close();
        co_return;
    }
    std::println("TLS handshake OK");

    // Echo 循环
    std::array<std::byte, 4096> buf{};
    for (;;) {
        auto rd = co_await stream.async_read(mutable_buffer{buf.data(), buf.size()});
        if (!rd || *rd == 0) break;

        auto wr = co_await stream.async_write(
            const_buffer{buf.data(), *rd});
        if (!wr) break;
    }

    // 优雅关闭
    (void)co_await stream.async_shutdown();
    client_sock.close();
    std::println("client disconnected");
}

auto accept_loop(io_context& ctx, socket& listener, ssl_context& ssl_ctx)
    -> task<void>
{
    for (;;) {
        auto r = co_await async_accept(ctx, listener);
        if (!r) {
            std::println(stderr, "accept error: {}", r.error().message());
            continue;
        }
        spawn(ctx, handle_client(ctx, std::move(*r), ssl_ctx));
    }
}

#endif // CNETMOD_HAS_SSL

int main(int argc, char* argv[]) {
#ifndef CNETMOD_HAS_SSL
    std::println(stderr, "SSL support not available (build with -DCNETMOD_ENABLE_SSL=ON and OpenSSL)");
    return 1;
#else
    if (argc < 3) {
        std::println(stderr, "usage: {} <cert.pem> <key.pem> [port]", argv[0]);
        return 1;
    }

    net_init net;

    std::string_view cert_path = argv[1];
    std::string_view key_path  = argv[2];
    std::uint16_t port = argc > 3 ? static_cast<std::uint16_t>(std::atoi(argv[3])) : 8443;

    // 创建 SSL 服务器上下文
    auto ssl_ctx_r = ssl_context::server();
    if (!ssl_ctx_r) {
        std::println(stderr, "ssl_context::server() failed: {}", ssl_ctx_r.error().message());
        return 1;
    }
    auto& ssl_ctx = *ssl_ctx_r;

    auto r1 = ssl_ctx.load_cert_file(cert_path);
    if (!r1) {
        std::println(stderr, "load_cert_file failed: {}", r1.error().message());
        return 1;
    }
    auto r2 = ssl_ctx.load_key_file(key_path);
    if (!r2) {
        std::println(stderr, "load_key_file failed: {}", r2.error().message());
        return 1;
    }

    // 创建监听 socket
    auto sock_r = socket::create(address_family::ipv4, socket_type::stream);
    if (!sock_r) {
        std::println(stderr, "socket create failed");
        return 1;
    }
    auto& listener = *sock_r;

    auto addr = ip_address::from_string("*********");
    (void)listener.bind(endpoint{*addr, port});
    (void)listener.listen();
    std::println("SSL echo server listening on port {}", port);

    auto ctx = make_io_context();
    spawn(*ctx, accept_loop(*ctx, listener, ssl_ctx));
    ctx->run();

    return 0;
#endif
}
