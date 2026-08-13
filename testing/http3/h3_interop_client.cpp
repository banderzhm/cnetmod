// Standalone cnetmod HTTP/3 client used by the release interoperability gate.

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.session;

auto main(int argc, char** argv) -> int
{
    logger::init("h3_interop_client");
    if (argc != 4 && (argc != 5 || std::string_view{argv[4]} != "--webtransport"))
    {
        logger::error("usage: h3_interop_client <host> <port> <path> [--webtransport]");
        logger::flush();
        logger::shutdown();
        return 2;
    }
    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    const bool webtransport_echo = argc == 5;
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        logger::error("TLS context creation failed: {}", tls_result.error().message());
        logger::flush();
        logger::shutdown();
        return 1;
    }
    auto tls = std::move(*tls_result);
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = host;
    options.max_datagram_frame_size = webtransport_echo ? 1200U : 0U;
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
            logger::error("connect failed: {}", connected.error().message());
            context->stop();
            co_return;
        }
        if (webtransport_echo)
        {
            auto request = cnetmod::http::v3::make_webtransport_connect_request(host, argv[3]);
            request.port = port;
            auto session = co_await client.connect_webtransport(request);
            if (!session)
            {
                logger::error("WebTransport CONNECT failed: {}", session.error().message());
                context->stop();
                co_return;
            }
            const auto child = co_await session->open_bidirectional_stream();
            if (!child)
            {
                logger::error("WebTransport child-stream open failed: {}", child.error().message());
                context->stop();
                co_return;
            }
            const std::array<std::byte, 4> child_message{
                std::byte{'s'}, std::byte{'t'}, std::byte{'r'}, std::byte{'m'}};
            if (const auto sent = co_await session->send_stream(*child,
                    cnetmod::byte_view{child_message.data(), child_message.size()});
                !sent)
            {
                logger::error("WebTransport child-stream send failed: {}", sent.error().message());
                context->stop();
                co_return;
            }
            if (const auto child = co_await session->open_unidirectional_stream(); !child)
            {
                logger::error("WebTransport unidirectional child-stream open failed: {}",
                    child.error().message());
                context->stop();
                co_return;
            }
            const std::array<std::byte, 4> message{
                std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
            if (const auto sent = co_await session->send_datagram(
                    cnetmod::byte_view{message.data(), message.size()});
                !sent)
            {
                logger::error("WebTransport Datagram send failed: {}", sent.error().message());
                context->stop();
                co_return;
            }
            std::array<std::byte, 4> child_echo{};
            const auto child_received = co_await session->receive_stream(*child,
                cnetmod::mutable_buffer{child_echo.data(), child_echo.size()});
            if (!child_received || *child_received != child_message.size() ||
                !std::equal(child_echo.begin(), child_echo.end(), child_message.begin()))
            {
                if (!child_received)
                    logger::error("WebTransport child-stream echo failed: {}",
                        child_received.error().message());
                else
                    logger::error("WebTransport child-stream echo had an unexpected payload");
                context->stop();
                co_return;
            }
            const auto echoed = co_await session->receive_datagram();
            if (!echoed || echoed->size() != message.size() ||
                !std::equal(echoed->begin(), echoed->end(), message.begin()))
            {
                if (!echoed)
                    logger::error("WebTransport Datagram echo failed: {}",
                        echoed.error().message());
                else
                    logger::error("WebTransport Datagram echo had an unexpected payload");
                context->stop();
                co_return;
            }
            const auto close = co_await session->wait_for_close();
            if (!close || close->application_error_code != 7U ||
                close->reason != "echo 已完成")
            {
                logger::error("WebTransport close capsule failed");
                context->stop();
                co_return;
            }
            logger::info("WebTransport child-stream and Datagram echo: ok");
            co_await client.close();
            exit_code = 0;
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
            logger::error("request failed: {}", response.error().message());
            context->stop();
            co_return;
        }
        logger::info("Status: {}", response->status);
        logger::info("Body: {}", response->body);
        co_await client.close();
        exit_code = response->status >= 200 && response->status < 300 ? 0 : 1;
        context->stop();
    };
    cnetmod::spawn(*context, execute());
    context->run();
    logger::flush();
    logger::shutdown();
    return exit_code;
}
