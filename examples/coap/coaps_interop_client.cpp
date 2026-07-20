#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.core.net_init;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.coap;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cn = cnetmod;

#ifdef CNETMOD_HAS_SSL

auto run_client(cn::io_context& ctx,
                std::uint16_t port,
                int& exit_code) -> cn::task<void>
{
    auto addr = cn::ip_address::from_string("127.0.0.1");
    if (!addr) {
        std::println("COAPS_CLIENT_ERROR addr {}", addr.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    auto ssl_ctx_r = cn::ssl_context::dtls_client();
    if (!ssl_ctx_r) {
        std::println("COAPS_CLIENT_ERROR context {}", ssl_ctx_r.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }
    cn::coap::secure_client_config cfg;
    cfg.security = cn::coap::coaps_security_config::insecure_for_testing();

    cn::coap::secure_client client{ctx, *ssl_ctx_r, cfg};
    auto remote = cn::endpoint{*addr, port};
    auto resp = co_await client.get(remote, "/secure");
    if (!resp) {
        std::println("COAPS_CLIENT_ERROR get {}", resp.error().message());
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    auto body = cn::coap::payload_text(*resp);
    std::println("COAPS_CLIENT_BODY {}", body);
    exit_code = body == "coaps-python-22.5" ? 0 : 2;
    ctx.stop();
}

#endif

auto main(int argc, char** argv) -> int
{
#ifndef CNETMOD_HAS_SSL
    std::println("COAPS_CLIENT_ERROR SSL support is disabled");
    return 1;
#else
    cn::net_init net;
    const auto port = argc > 1
        ? static_cast<std::uint16_t>(std::stoul(argv[1]))
        : static_cast<std::uint16_t>(cn::coap::default_secure_port);

    auto ctx = cn::make_io_context();
    int exit_code = 1;
    cn::spawn(*ctx, run_client(*ctx, port, exit_code));
    ctx->run();
    return exit_code;
#endif
}
