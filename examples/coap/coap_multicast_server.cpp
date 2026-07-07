#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core;
import cnetmod.core.net_init;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto run_server(cn::io_context& ctx, std::uint16_t port) -> cn::task<void> {
    cn::coap::server_config cfg;
    cfg.enable_observe = false;
    auto server = std::make_shared<cn::coap::server>(ctx, cfg);
    auto listen = server->listen("0.0.0.0", port);
    if (!listen) {
        std::println("COAP_MULTICAST_SERVER_ERROR listen {}", listen.error().message());
        std::fflush(stdout);
        ctx.stop();
        co_return;
    }

    auto group = cn::ip_address::from_string("224.0.1.187");
    if (!group) {
        std::println("COAP_MULTICAST_SERVER_ERROR group {}", group.error().message());
        std::fflush(stdout);
        ctx.stop();
        co_return;
    }
    if (auto joined = server->join_multicast_group(*group); !joined) {
        std::println("COAP_MULTICAST_SERVER_ERROR join {}", joined.error().message());
        std::fflush(stdout);
        ctx.stop();
        co_return;
    }

    server->route(cn::coap::method::get, "/.well-known/core",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            co_return cn::coap::text_response(req.request,
                "</sensors/temp>;rt=\"temperature-c\"",
                cn::coap::response_code::content);
        });
    server->route(cn::coap::method::get, "/sensors/temp",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            co_return cn::coap::text_response(req.request, "multicast-22.5");
        });

    std::println("COAP_MULTICAST_SERVER_READY {}", port);
    std::fflush(stdout);
    co_await server->run();
}

auto main(int argc, char** argv) -> int {
    cn::net_init net;
    const auto port = argc > 1
        ? static_cast<std::uint16_t>(std::stoul(argv[1]))
        : static_cast<std::uint16_t>(cn::coap::default_port);

    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_server(*ctx, port));
    ctx->run();
    return 0;
}
