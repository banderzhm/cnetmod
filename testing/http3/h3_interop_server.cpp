// Standalone cnetmod HTTP/3 server used by the release interoperability gate.

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.session;

auto main(int argc, char** argv) -> int
{
    std::uint16_t port = 4433;
    unsigned workers = 1;
    bool webtransport_echo{};
    std::string certificate = "cert.pem";
    std::string private_key = "key.pem";
    for (int index = 1; index < argc; ++index)
    {
        if (std::string_view{argv[index]} == "--port" && index + 1 < argc)
        {
            port = static_cast<std::uint16_t>(std::stoul(argv[++index]));
        }
        else if (std::string_view{argv[index]} == "--workers" && index + 1 < argc)
        {
            workers = std::max(1U, static_cast<unsigned>(std::stoul(argv[++index])));
        }
        else if (std::string_view{argv[index]} == "--cert" && index + 1 < argc)
        {
            certificate = argv[++index];
        }
        else if (std::string_view{argv[index]} == "--key" && index + 1 < argc)
        {
            private_key = argv[++index];
        }
        else if (std::string_view{argv[index]} == "--webtransport")
        {
            webtransport_echo = true;
        }
    }

    cnetmod::net_init network;
    auto tls_result = cnetmod::ssl_context::quic_server();
    if (!tls_result)
    {
        logger::error{"TLS context creation failed: {}", tls_result.error().message()};
        return 1;
    }
    auto tls = std::move(*tls_result);
    // A QUIC TLS handshake does not infer HTTP/3 from the transport.  Without
    // this selection callback BoringSSL rejects the client's only offer
    // (`h3`) with no_application_protocol after the Retry-validated Initial.
    tls.configure_alpn_server({"h3"});
    if (auto result = tls.load_cert_file(certificate); !result)
    {
        logger::error{"certificate load failed: {}", result.error().message()};
        return 1;
    }
    if (auto result = tls.load_key_file(private_key); !result)
    {
        logger::error{"private-key load failed: {}", result.error().message()};
        return 1;
    }
    const auto address = cnetmod::ip_address::from_string("0.0.0.0");
    if (!address)
    {
        logger::error{"listener address creation failed"};
        return 1;
    }

    auto handler = [](cnetmod::http::v3::http3_request& request,
                       cnetmod::http::v3::http3_response& response) -> std::error_code
    {
        if (request.path == "/health")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = "ok\n";
            return {};
        }
        if (request.path == "/hello")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = "Hello, World!";
            return {};
        }
        response.status = 404;
        response.body = "not found\n";
        return {};
    };
    cnetmod::http::v3::async_webtransport_handler webtransport_handler =
        [](cnetmod::http::v3::http3_request&,
            cnetmod::http::v3::webtransport_session& session,
            cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
    {
        // QUIC does not order independent streams.  The probe opens a
        // bidirectional data child and a unidirectional child together,
        // so accept both first and identify them by stream direction
        // rather than relying on packet arrival order.
        const auto first_child = co_await session.accept_stream();
        if (!first_child)
            co_return std::unexpected(first_child.error());
        const auto second_child = co_await session.accept_stream();
        if (!second_child)
            co_return std::unexpected(second_child.error());
        const auto first_is_unidirectional = (*first_child & 0x02U) != 0U;
        const auto second_is_unidirectional = (*second_child & 0x02U) != 0U;
        if (first_is_unidirectional == second_is_unidirectional)
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        const auto child = first_is_unidirectional ? *second_child : *first_child;
        std::array<std::byte, 4> child_payload{};
        const auto child_read = co_await session.receive_stream(child,
            cnetmod::mutable_buffer{child_payload.data(), child_payload.size()});
        const std::array<std::byte, 4> expected_child{
            std::byte{'s'}, std::byte{'t'}, std::byte{'r'}, std::byte{'m'}};
        if (!child_read || *child_read != expected_child.size() ||
            !std::equal(child_payload.begin(), child_payload.end(), expected_child.begin()))
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        const auto child_echo = co_await session.send_stream(child,
            cnetmod::byte_view{expected_child.data(), expected_child.size()}, true);
        if (!child_echo)
            co_return std::unexpected(child_echo.error());
        auto payload = co_await session.receive_datagram();
        if (!payload)
            co_return std::unexpected(payload.error());
        const auto sent = co_await session.send_datagram(
            cnetmod::byte_view{payload->data(), payload->size()});
        if (!sent)
            co_return std::unexpected(sent.error());
        // A WebTransport DATAGRAM is intentionally unordered and
        // unreliable. A close capsule may overtake the echo, so the peer
        // confirms that it consumed the echo before this fixture closes
        // the session. This validates both DATAGRAM and close-capsule
        // handling without making their cross-stream timing observable.
        const auto confirmation = co_await session.receive_datagram();
        const std::array<std::byte, 9> expected_confirmation{
            std::byte{'c'}, std::byte{'l'}, std::byte{'o'}, std::byte{'s'},
            std::byte{'e'}, std::byte{'-'}, std::byte{'a'}, std::byte{'c'},
            std::byte{'k'}};
        if (!confirmation || confirmation->size() != expected_confirmation.size() ||
            !std::equal(confirmation->begin(), confirmation->end(),
                expected_confirmation.begin()))
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        co_return co_await session.close(7U, "echo complete");
    };
    cnetmod::http::v3::async_server_request_handler async_handler =
        [handler](cnetmod::http::v3::http3_request& request,
            cnetmod::http::v3::http3_response& response,
            cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
    {
        if (const auto error = handler(request, response); error)
            co_return std::unexpected(error);
        co_return {};
    };

    if (workers == 1U)
    {
        auto context = cnetmod::make_io_context();
        auto server = webtransport_echo
            ? cnetmod::http::v3::make_http3_server(*context, tls,
                  cnetmod::endpoint{*address, port},
                  cnetmod::http::v3::http3_server_handlers{async_handler, webtransport_handler})
            : cnetmod::http::v3::make_http3_server(*context, tls,
                  cnetmod::endpoint{*address, port}, handler);
        if (webtransport_echo)
            (void)server->set_max_datagram_frame_size(1200U);
        if (auto result = server->start(); !result)
        {
            logger::error{"server start failed: {}", result.error().message()};
            return 1;
        }
        logger::info{"HTTP/3 interop listener started on UDP {} with 1 worker", port};
        context->run();
        return 0;
    }

    cnetmod::server_context context{workers, workers};
    auto server = webtransport_echo
        ? cnetmod::http::v3::make_http3_server(context, tls,
              cnetmod::endpoint{*address, port}, cnetmod::http::v3::http3_server_handlers{std::move(async_handler), std::move(webtransport_handler)})
        : cnetmod::http::v3::make_http3_server(context, tls,
              cnetmod::endpoint{*address, port}, std::move(handler));
    if (webtransport_echo)
        (void)server->set_max_datagram_frame_size(1200U);
    if (auto result = server->start(); !result)
    {
        logger::error{"server start failed: {}", result.error().message()};
        return 1;
    }
    logger::info{"HTTP/3 interop listener started on UDP {} with {} workers", port,
        workers};
    context.run();
    return 0;
}
