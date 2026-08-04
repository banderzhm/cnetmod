// Standalone cnetmod HTTP/3 server used by the release interoperability gate.

import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.session;

auto main(int argc, char** argv) -> int
{
    std::uint16_t port = 4433;
    unsigned workers = 1;
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
    }

    cnetmod::net_init network;
    auto tls_result = cnetmod::ssl_context::quic_server();
    if (!tls_result)
    {
        std::cerr << "TLS context creation failed: " << tls_result.error().message() << '\n';
        return 1;
    }
    auto tls = std::move(*tls_result);
    // A QUIC TLS handshake does not infer HTTP/3 from the transport.  Without
    // this selection callback BoringSSL rejects the client's only offer
    // (`h3`) with no_application_protocol after the Retry-validated Initial.
    tls.configure_alpn_server({"h3"});
    if (auto result = tls.load_cert_file(certificate); !result)
    {
        std::cerr << "certificate load failed: " << result.error().message() << '\n';
        return 1;
    }
    if (auto result = tls.load_key_file(private_key); !result)
    {
        std::cerr << "private-key load failed: " << result.error().message() << '\n';
        return 1;
    }
    const auto address = cnetmod::ip_address::from_string("0.0.0.0");
    if (!address)
    {
        std::cerr << "listener address creation failed\n";
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
    if (workers == 1U)
    {
        auto context = cnetmod::make_io_context();
        auto server = cnetmod::http::v3::make_http3_server(*context, tls,
            cnetmod::endpoint{*address, port}, handler);
        if (auto result = server->start(); !result)
        {
            std::cerr << "server start failed: " << result.error().message() << '\n';
            return 1;
        }
        std::cout << "listening " << port << " with 1 worker" << std::endl;
        context->run();
        return 0;
    }

    cnetmod::server_context context{workers, workers};
    auto server = cnetmod::http::v3::make_http3_server(context, tls,
        cnetmod::endpoint{*address, port}, std::move(handler));
    if (auto result = server->start(); !result)
    {
        std::cerr << "server start failed: " << result.error().message() << '\n';
        return 1;
    }
    std::cout << "listening " << port << " with " << workers << " workers" << std::endl;
    context.run();
    return 0;
}
