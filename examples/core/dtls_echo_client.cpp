#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.core.net_init;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
import cnetmod.core.dtls;
#endif

namespace cn = cnetmod;

#ifdef CNETMOD_HAS_SSL

auto run_client(cn::io_context& ctx,
                std::string host,
                std::uint16_t port,
                std::string payload,
                int& exit_code) -> cn::task<void>
{
    auto sock_r = cn::socket::create(cn::address_family::ipv4, cn::socket_type::datagram);
    if (!sock_r) {
        std::println("DTLS_CLIENT_ERROR socket {}", sock_r.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }
    auto sock = std::move(*sock_r);

    auto ip = cn::ip_address::from_string(host);
    if (!ip) {
        std::println("DTLS_CLIENT_ERROR host {}", ip.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    auto ctx_r = cn::ssl_context::dtls_client();
    if (!ctx_r) {
        std::println("DTLS_CLIENT_ERROR context {}", ctx_r.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }
    ctx_r->set_verify_peer(false);

    cn::dtls_datagram_session session{
        *ctx_r,
        ctx,
        sock,
        cn::endpoint{*ip, port},
        cn::dtls_role::client,
    };

    auto hs = co_await session.async_handshake();
    if (!hs) {
        std::println("DTLS_CLIENT_ERROR handshake {}", hs.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    auto wr = co_await session.async_write(
        cn::const_buffer{payload.data(), payload.size()});
    if (!wr) {
        std::println("DTLS_CLIENT_ERROR write {}", wr.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    std::array<std::byte, 4096> in{};
    auto rd = co_await session.async_read(cn::mutable_buffer{in.data(), in.size()});
    if (!rd) {
        std::println("DTLS_CLIENT_ERROR read {}", rd.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    std::string body(reinterpret_cast<const char*>(in.data()), *rd);
    std::println("DTLS_CLIENT_ECHO {}", body);
    exit_code = body == payload ? 0 : 2;
    ctx.stop();
}

#endif

auto main(int argc, char** argv) -> int
{
#ifndef CNETMOD_HAS_SSL
    std::println("DTLS_CLIENT_ERROR SSL support is disabled");
    return 1;
#else
    cn::net_init net;
    const auto host = argc > 1 ? std::string{argv[1]} : std::string{"127.0.0.1"};
    const auto port = argc > 2
        ? static_cast<std::uint16_t>(std::stoul(argv[2]))
        : static_cast<std::uint16_t>(56840);
    const auto payload = argc > 3 ? std::string{argv[3]} : std::string{"dtls-udp-ssl"};

    auto ctx = cn::make_io_context();
    int exit_code = 1;
    cn::spawn(*ctx, run_client(*ctx, host, port, payload, exit_code));
    ctx->run();
    return exit_code;
#endif
}
