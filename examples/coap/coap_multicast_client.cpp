#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core;
import cnetmod.core.net_init;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto run_client(cn::io_context& ctx, std::uint16_t port, int& exit_code) -> cn::task<void> {
    cn::coap::multicast_client_config cfg;
    cfg.max_responses = 4;
    cfg.response_timeout = std::chrono::milliseconds{2500};

    cn::coap::multicast client{ctx, cfg};
    auto responses = co_await client.get(cn::coap::all_coap_nodes_ipv4(port), "/sensors/temp");
    if (!responses) {
        std::println("COAP_MULTICAST_CLIENT_ERROR {}", responses.error().message());
        std::fflush(stdout);
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    std::println("COAP_MULTICAST_CLIENT_COUNT {}", responses->size());
    for (const auto& item : *responses) {
        std::println("COAP_MULTICAST_CLIENT_BODY {} {}",
            item.peer.to_string(),
            cn::coap::payload_text(item.response));
    }
    std::fflush(stdout);
    exit_code = responses->empty() ? 2 : 0;
    ctx.stop();
}

auto main(int argc, char** argv) -> int {
    cn::net_init net;
    const auto port = argc > 1
        ? static_cast<std::uint16_t>(std::stoul(argv[1]))
        : static_cast<std::uint16_t>(cn::coap::default_port);

    auto ctx = cn::make_io_context();
    int exit_code = 1;
    cn::spawn(*ctx, run_client(*ctx, port, exit_code));
    ctx->run();
    return exit_code;
}
