#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;

namespace cn = cnetmod;
namespace grpc = cnetmod::grpc;
namespace proto = cnetmod::grpc::proto;

static auto say_handler(std::span<const std::byte> payload,
                        const grpc::call_context&)
    -> cn::task<std::expected<grpc::byte_buffer, grpc::status>>
{
    auto fields = proto::decode_message(payload);
    if (!fields) {
        co_return std::unexpected(grpc::make_status(
            grpc::status_code::invalid_argument, "invalid EchoRequest"));
    }

    std::string name;
    std::uint64_t sequence = 0;
    if (auto* f = proto::find_first(*fields, 1)) {
        name = proto::field_string(*f).value_or("");
    }
    if (auto* f = proto::find_first(*fields, 2)) {
        sequence = proto::field_uint64(*f).value_or(0);
    }

    grpc::byte_buffer out;
    proto::append_string(out, 1, "hello " + name);
    proto::append_uint64(out, 2, sequence);
    co_return out;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::println(stderr, "usage: grpc_interop_server <port>");
        return 2;
    }

    std::uint16_t port = 0;
    auto port_text = std::string_view(argv[1]);
    auto [ptr, ec] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (ec != std::errc{} || ptr != port_text.data() + port_text.size() || port == 0) {
        std::println(stderr, "invalid port");
        return 2;
    }

    cn::net_init net;
    auto ctx = cn::make_io_context();

    grpc::service_router grpc_router;
    grpc_router.add_unary("cnetmod.testing.grpc.EchoService", "Say", say_handler);
    grpc::health::registry health;
    health.set("cnetmod.testing.grpc.EchoService", grpc::health::serving_status::serving);
    grpc::health::register_service(grpc_router, health);
    grpc::reflection::install_service(grpc_router, {"cnetmod.testing.grpc.EchoService"});

    cn::http::router router;
    router.any("/*path", grpc_router.make_http_handler());

    cn::http::server server(*ctx);
    auto listen = server.listen("127.0.0.1", port);
    if (!listen) {
        std::println(stderr, "listen failed: {}", listen.error().message());
        return 1;
    }
    server.set_router(std::move(router));

    std::println("READY {}", port);
    std::fflush(stdout);

    cn::spawn(*ctx, server.run());
    ctx->run();
    return 0;
}
