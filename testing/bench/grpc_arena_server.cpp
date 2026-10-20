#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cn = cnetmod;
namespace grpc = cnetmod::grpc;

namespace {

auto get_sum(std::span<const std::byte> payload)
    -> std::expected<grpc::byte_buffer, grpc::status>
{
    // The general framework visitor decodes every field, borrows unknown byte
    // fields, and preserves proto3 defaults and last-value-wins semantics.
    std::uint32_t a{}, b{};
    auto decoded = grpc::proto::visit_message(payload, [&](const grpc::proto::field_view& field)
        {
            if (field.type != grpc::proto::wire_type::varint)
                return;
            if (field.number == 1)
                a = static_cast<std::uint32_t>(field.varint_value);
            else if (field.number == 2)
                b = static_cast<std::uint32_t>(field.varint_value);
        });
    if (!decoded)
        return std::unexpected(grpc::make_status(grpc::status_code::invalid_argument,
            "invalid SumRequest"));
    const auto result = std::bit_cast<std::int32_t>(a + b);
    grpc::byte_buffer response;
    if (result != 0)
        grpc::proto::append_int64(response, 1, result);
    return response;
}

template <class T>
auto number(std::string_view value, T& out) -> bool
{
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), out);
    return error == std::errc{} && end == value.data() + value.size() && out != 0;
}

} // namespace

auto main(int argc, char** argv) -> int
{
    cn::net_init net;
    logger::init("grpc-arena", logger::level::info);
    std::uint16_t port{};
    unsigned workers{};
    if ((argc != 3 && argc != 5) || !number(argv[1], port) || !number(argv[2], workers))
    {
        logger::error{"usage: grpc_arena_server <port> <workers> [<cert.pem> <key.pem>]"};
        logger::shutdown();
        return 2;
    }
    const auto* affinity_value = std::getenv("CNETMOD_GRPC_PIN_WORKERS");
    const bool pin_workers = affinity_value && std::string_view{affinity_value} == "1";
    cn::server_context context{workers, workers,
        {.enabled = pin_workers, .auto_assign_workers = pin_workers}};
    grpc::service_router service;
    service.add_unary_sync("benchmark.BenchmarkService", "GetSum", get_sum);
    cn::http::router router;
    router.any_sync("/benchmark.BenchmarkService/GetSum", service.make_http_sync_handler());
    router.any("/*path", service.make_http_handler());
#ifdef CNETMOD_HAS_SSL
    std::optional<cn::ssl_context> tls;
#endif
    cn::http::server server{context};
    server.set_router(std::move(router));
    server.set_h2_sync_adapter(service.make_h2_sync_handler(), service.make_h2_request_filter());
#ifdef CNETMOD_HAS_SSL
    if (argc == 5)
    {
        auto created = cn::ssl_context::server();
        if (!created)
        {
            logger::error{"TLS context creation failed: {}", created.error().message()};
            logger::shutdown();
            return 3;
        }
        tls.emplace(std::move(*created));
        if (!tls->load_cert_file(argv[3]) || !tls->load_key_file(argv[4]))
        {
            logger::error{"TLS certificate/key loading failed"};
            logger::shutdown();
            return 3;
        }
        tls->configure_alpn_server({"h2"});
        server.set_ssl_context(*tls);
    }
#else
    if (argc == 5)
    {
        logger::error{"TLS requested in a build without SSL support"};
        logger::shutdown();
        return 3;
    }
#endif
    auto listening = server.listen("::", port, {.ipv6_only = false});
    if (!listening)
    {
        logger::error{"listen failed: {}", listening.error().message()};
        logger::shutdown();
        return 4;
    }
    cn::spawn(context.accept_io(), server.run());
    logger::info{"READY port={} workers={} tls={} pin_workers={}", port, workers, argc == 5, pin_workers};
    context.run();
    logger::shutdown();
    return 0;
}
