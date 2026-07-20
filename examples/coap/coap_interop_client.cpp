#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.core.net_init;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto run_client(cn::io_context& ctx, std::uint16_t port) -> cn::task<void>
{
    cn::coap::client client(ctx);
    auto remote = co_await client.resolve_endpoint("127.0.0.1", port);
    if (!remote) {
        std::println("COAP_CLIENT_ERROR {}", remote.error().message());
        ctx.stop();
        co_return;
    }

    auto response = co_await client.get(*remote, "/sensors/temp");
    if (!response) {
        std::println("COAP_CLIENT_ERROR {}", response.error().message());
        ctx.stop();
        co_return;
    }

    std::println("COAP_CLIENT_BODY {}", cn::coap::payload_text(*response));

    auto large = co_await client.get_blockwise(*remote, "/large", 4);
    if (!large) {
        std::println("COAP_CLIENT_ERROR {}", large.error().message());
        ctx.stop();
        co_return;
    }

    std::println("COAP_CLIENT_LARGE {}", large->payload.size());

    std::string upload;
    upload.reserve(1800);
    for (int i = 0; i < 90; ++i) {
        upload += "cpp-block1-upload;";
    }

    auto uploaded = co_await client.post_blockwise(*remote, "/upload",
        cn::coap::to_bytes(upload), cn::coap::content_format::text_plain, 4);
    if (!uploaded) {
        std::println("COAP_CLIENT_ERROR {}", uploaded.error().message());
        ctx.stop();
        co_return;
    }

    std::println("COAP_CLIENT_UPLOAD {}", cn::coap::payload_text(*uploaded));
    ctx.stop();
}

auto main(int argc, char** argv) -> int
{
    cn::net_init net;
    const auto port = argc > 1
        ? static_cast<std::uint16_t>(std::stoul(argv[1]))
        : static_cast<std::uint16_t>(56831);
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_client(*ctx, port));
    ctx->run();
    return 0;
}
