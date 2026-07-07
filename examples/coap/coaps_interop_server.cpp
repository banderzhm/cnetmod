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

auto run_server(cn::io_context& ctx,
                cn::ssl_context& ssl_ctx,
                std::uint16_t port) -> cn::task<void>
{
    auto server = std::make_shared<cn::coap::secure_server>(ctx, ssl_ctx);
    auto listen = server->listen("127.0.0.1", port);
    if (!listen) {
        std::println("COAPS_SERVER_ERROR {}", listen.error().message());
        ctx.stop();
        co_return;
    }

    server->route(cn::coap::method::get, "/secure",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            co_return cn::coap::text_response(req.request, "coaps-secure-22.5");
        });

    server->route(cn::coap::method::post, "/secure/upload",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            co_return cn::coap::text_response(req.request,
                std::format("coaps-upload-{}", req.request.payload.size()));
        });

    std::println("COAPS_SERVER_READY {}", port);
    co_await server->run();
}

#endif

auto main(int argc, char** argv) -> int
{
#ifndef CNETMOD_HAS_SSL
    std::println("COAPS_SERVER_ERROR SSL support is disabled");
    return 1;
#else
    cn::net_init net;
    const auto cert = argc > 1 ? std::string_view{argv[1]} : std::string_view{"examples/test_ssl/cert.pem"};
    const auto key = argc > 2 ? std::string_view{argv[2]} : std::string_view{"examples/test_ssl/key.pem"};
    const auto port = argc > 3
        ? static_cast<std::uint16_t>(std::stoul(argv[3]))
        : static_cast<std::uint16_t>(cn::coap::default_secure_port);

    auto ssl_ctx_r = cn::ssl_context::dtls_server();
    if (!ssl_ctx_r) {
        std::println("COAPS_SERVER_ERROR context {}", ssl_ctx_r.error().message());
        return 1;
    }
    auto& ssl_ctx = *ssl_ctx_r;
    if (auto r = ssl_ctx.load_cert_file(cert); !r) {
        std::println("COAPS_SERVER_ERROR cert {}", r.error().message());
        return 1;
    }
    if (auto r = ssl_ctx.load_key_file(key); !r) {
        std::println("COAPS_SERVER_ERROR key {}", r.error().message());
        return 1;
    }

    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_server(*ctx, ssl_ctx, port));
    ctx->run();
    return 0;
#endif
}
