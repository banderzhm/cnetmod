// End-to-end QUIC connection migration probe.
//
// The Python UDP proxy changes the source port used towards the server after
// the first request.  The client keeps the same QUIC connection and must
// complete the second HTTP/3 request after path validation.

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.session;

namespace {

auto expect_response(const std::expected<cnetmod::http::v3::http3_response,
                         std::error_code>& result,
    int status, std::string_view body,
    std::string_view label) -> bool
{
    if (!result)
    {
        logger::error("{} failed: {}", label, result.error().message());
        return false;
    }
    if (result->status != status || result->body != body)
    {
        logger::error("{} returned status/body {}/{}", label, result->status,
            result->body);
        return false;
    }
    return true;
}

} // namespace

auto main(int argc, char** argv) -> int
{
    logger::init("http3_migration_e2e");
    if (argc != 4)
    {
        logger::error("usage: http3_migration_e2e <host> <proxy-port> <marker>");
        logger::flush();
        logger::shutdown();
        return 2;
    }
    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    const std::filesystem::path marker = argv[3];

    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        logger::error("TLS context creation failed: {}",
            tls_result.error().message());
        logger::flush();
        logger::shutdown();
        return 1;
    }
    auto tls = std::move(*tls_result);
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = host;
    options.request_timeout = std::chrono::seconds{10};
    cnetmod::http::v3::http3_client client{*context, tls, std::move(options)};

    int exit_code = 1;
    auto execute = [&]() -> cnetmod::task<void>
    {
        const auto connected = co_await client.connect(host, port);
        if (!connected)
        {
            logger::error("connect failed: {}", connected.error().message());
            context->stop();
            co_return;
        }

        cnetmod::http::v3::http3_request first;
        first.host = host;
        first.port = port;
        first.path = "/health";
        if (!expect_response(co_await client.send_request(first), 200, "ok\n",
                "first request"))
        {
            co_await client.close();
            context->stop();
            co_return;
        }

        // Tell the proxy to use a fresh upstream UDP socket.  Keep the QUIC
        // connection alive while the server validates the new path.
        {
            std::ofstream signal(marker, std::ios::binary | std::ios::trunc);
            signal << "switch\n";
        }
        (void)co_await cnetmod::async_timer_wait(*context,
            std::chrono::milliseconds{150});

        cnetmod::http::v3::http3_request second;
        second.host = host;
        second.port = port;
        second.path = "/hello";
        const auto result = co_await client.send_request(second);
        if (expect_response(result, 200, "Hello, World!", "migrated request"))
            exit_code = 0;
        co_await client.close();
        context->stop();
    };
    cnetmod::spawn(*context, execute());
    context->run();
    logger::flush();
    logger::shutdown();
    return exit_code;
}
