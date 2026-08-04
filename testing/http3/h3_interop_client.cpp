// Standalone cnetmod HTTP/3 client used by the release interoperability gate.

import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.session;

auto main(int argc, char** argv) -> int
{
    if (argc != 4)
    {
        std::cerr << "usage: h3_interop_client <host> <port> <path>\n";
        return 2;
    }
    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        std::cerr << "TLS context creation failed: " << tls_result.error().message() << '\n';
        return 1;
    }
    auto tls = std::move(*tls_result);
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = host;
    cnetmod::http::v3::http3_client client{*context, tls, std::move(options)};

    // Both the application coroutine and its UDP/timer completions must run
    // on the same io_context thread.  io_uring submission queues are not a
    // cross-thread coroutine scheduler, so wrapping connect()/send_request()
    // in sync_wait on a second thread races the ring and can leave the Initial
    // packet unsent.  Spawn one root coroutine and stop the loop on completion.
    int exit_code = 1;
    auto execute = [&]() -> cnetmod::task<void>
    {
        const auto connected = co_await client.connect(host, port);
        if (!connected)
        {
            std::cerr << "connect failed: " << connected.error().message() << '\n';
            context->stop();
            co_return;
        }
        cnetmod::http::v3::http3_request request;
        request.path = argv[3];
        request.host = host;
        request.port = port;
        const auto response = co_await client.send_request(request);
        if (!response)
        {
            std::cerr << "request failed: " << response.error().message() << '\n';
            context->stop();
            co_return;
        }
        std::cout << "Status: " << response->status << '\n';
        std::cout << "Body: " << response->body << '\n';
        co_await client.close();
        exit_code = response->status >= 200 && response->status < 300 ? 0 : 1;
        context->stop();
    };
    cnetmod::spawn(*context, execute());
    context->run();
    return exit_code;
}
