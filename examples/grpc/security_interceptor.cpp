#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cn = cnetmod;
namespace grpc = cnetmod::grpc;

static auto require_bearer(std::string expected_token) -> grpc::server_interceptor {
    return [expected = std::move(expected_token)](const grpc::call_context& call)
        -> std::expected<void, grpc::status> {
        auto auth = grpc::metadata_value(call.headers, "authorization");
        if (auth != expected) {
            return std::unexpected(grpc::make_status(
                grpc::status_code::unauthenticated,
                "missing or invalid bearer token"));
        }
        return {};
    };
}

static auto inject_bearer(std::string token) -> grpc::client_request_interceptor {
    return [token = std::move(token)](grpc::client_call& call)
        -> std::expected<void, grpc::status> {
        call.headers.emplace("authorization", token);
        return {};
    };
}

static auto echo_handler(std::span<const std::byte> payload,
                         const grpc::call_context&)
    -> cn::task<std::expected<grpc::byte_buffer, grpc::status>>
{
    grpc::byte_buffer out(payload.begin(), payload.end());
    co_return out;
}

int main(int argc, char** argv) {
#ifndef CNETMOD_HAS_SSL
    (void)argc;
    (void)argv;
    std::println("grpc security interceptor example requires OpenSSL support.");
    return 0;
#else
    if (argc != 7) {
        std::println("usage: example_security_interceptor <port> <server.crt> <server.key> <ca.crt> <client.crt> <client.key>");
        return 2;
    }

    const auto port = static_cast<std::uint16_t>(std::stoul(argv[1]));
    const std::string server_cert = argv[2];
    const std::string server_key = argv[3];
    const std::string ca_cert = argv[4];
    const std::string client_cert = argv[5];
    const std::string client_key = argv[6];
    constexpr std::string_view token = "Bearer production-token";

    cn::net_init net;
    auto ctx = cn::make_io_context();

    auto ssl_ctx_r = cn::ssl_context::server();
    if (!ssl_ctx_r) {
        std::println(stderr, "ssl_context::server failed: {}", ssl_ctx_r.error().message());
        return 1;
    }
    auto& ssl_ctx = *ssl_ctx_r;
    if (!ssl_ctx.load_cert_file(server_cert) || !ssl_ctx.load_key_file(server_key) ||
        !ssl_ctx.load_ca_file(ca_cert)) {
        std::println(stderr, "failed to load server TLS material");
        return 1;
    }
    ssl_ctx.set_verify_peer(true);
    ssl_ctx.configure_alpn_server({"h2"});

    grpc::service_router grpc_router(grpc::server_options{
        .max_receive_message_bytes = 4 * 1024 * 1024,
        .max_send_message_bytes = 4 * 1024 * 1024,
        .max_metadata_bytes = 16 * 1024,
        .accept_gzip = true,
        .interceptors = {require_bearer(std::string(token))},
    });
    grpc_router.add_unary("example.secure.Echo", "Say", echo_handler);

    cn::http::router router;
    router.any("/*path", grpc_router.make_http_handler());

    cn::http::server srv(*ctx);
    auto listen = srv.listen("0.0.0.0", port);
    if (!listen) {
        std::println(stderr, "listen failed: {}", listen.error().message());
        return 1;
    }
    srv.set_ssl_context(ssl_ctx);
    srv.set_router(std::move(router));

    cn::http::client_options http_opts;
    http_opts.version_pref = cn::http::http_version_preference::http2_only;
    http_opts.ca_file = ca_cert;
    http_opts.cert_file = client_cert;
    http_opts.key_file = client_key;
    grpc::client_options client_opts;
    client_opts.http = std::move(http_opts);
    client_opts.request_interceptors.push_back(inject_bearer(std::string(token)));

    std::println("secure gRPC server configured on https://0.0.0.0:{}", port);
    std::println("client_opts demonstrates mTLS + auth interceptor setup for outbound calls.");
    (void)client_opts;

    cn::spawn(*ctx, srv.run());
    ctx->run();
    return 0;
#endif
}
