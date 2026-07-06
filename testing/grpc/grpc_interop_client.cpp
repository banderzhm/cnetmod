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

static auto run_client(cn::io_context& ctx, std::uint16_t port, int& exit_code)
    -> cn::task<void>
{
    cn::http::client_options http_opts;
    http_opts.version_pref = cn::http::http_version_preference::http2_only;
    http_opts.keep_alive = false;

    grpc::client client(ctx, std::format("http://127.0.0.1:{}", port),
                        grpc::client_options{.http = std::move(http_opts)});

    grpc::byte_buffer request;
    proto::append_string(request, 1, "cnetmod-client");
    proto::append_uint64(request, 2, 11);
    proto::append_bool(request, 3, true);

    auto response = co_await client.unary(grpc::unary_request{
        .service = "cnetmod.testing.grpc.EchoService",
        .method = "Say",
        .payload = std::move(request),
        .timeout = std::chrono::seconds{5},
    });

    if (!response) {
        std::println(stderr, "grpc call failed: {} {}", static_cast<int>(response.error().code),
                     response.error().message);
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    auto fields = proto::decode_message(response->payload);
    if (!fields) {
        std::println(stderr, "invalid response protobuf");
        exit_code = 1;
        ctx.stop();
        co_return;
    }

    auto* message_field = proto::find_first(*fields, 1);
    auto* sequence_field = proto::find_first(*fields, 2);
    auto message = message_field ? proto::field_string(*message_field).value_or("") : "";
    auto sequence = sequence_field ? proto::field_uint64(*sequence_field).value_or(0) : 0;
    if (message != "hello cnetmod-client" || sequence != 11) {
        std::println(stderr, "unexpected response: message={} sequence={}", message, sequence);
        exit_code = 1;
    }

    ctx.stop();
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::println(stderr, "usage: grpc_interop_client <port>");
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
    int exit_code = 0;
    cn::spawn(*ctx, run_client(*ctx, port, exit_code));
    ctx->run();
    return exit_code;
}
