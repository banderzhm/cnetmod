// Standalone cnetmod HTTP/3 server used by the release interoperability gate.

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.http.v3.session;

// This fixture samples from an internal timer: querying H3 periodically
// would itself trigger ingress cleanup and conceal a missing timer deadline.
auto sample_cold_cleanup(cnetmod::io_context& context,
    cnetmod::http::v3::http3_server& server, std::string report)
    -> cnetmod::task<void>
{
    std::string snapshots;
    // A confirmed connection can drain for three initial PTOs (~3s).
    // Sample reset retention after that legal silence window; the final
    // snapshot remains 35s after arming, with no inbound probe traffic.
    for (const auto delay : {std::chrono::seconds{4}, std::chrono::seconds{31}})
    {
        (void)co_await cnetmod::async_timer_wait(context, delay);
        const auto stats = server.statistics();
        snapshots += std::format("{} {} {} {} {} {}\n",
            stats.active_connections, stats.retained_reset_routes,
            stats.pending_initial_routes, stats.pending_connection_timers,
            stats.active_connection_inboxes, stats.retired_dispatch_routes);
        (void)co_await cnetmod::async_file_write_all(context, report, snapshots);
    }
}

// Exercise stop/start while the first generation still has a sleeping timer.
// The test waits on this file before sending its first datagram.
auto restart_listener(cnetmod::io_context& context,
    cnetmod::http::v3::http3_server& server, std::string report)
    -> cnetmod::task<void>
{
    (void)co_await cnetmod::async_timer_wait(context, std::chrono::milliseconds{20});
    co_await server.stop();
    const auto started = server.start();
    const auto stats = server.statistics();
    (void)co_await cnetmod::async_file_write_all(context, report + ".stats",
        std::format("{} {} {}\n", stats.active_connections,
            stats.active_connection_inboxes, stats.inbox_queued_datagrams));
    (void)co_await cnetmod::async_file_write_all(context, report,
        started ? "restarted\n" : "restart failed\n");
}

auto main(int argc, char** argv) -> int
{
    std::uint16_t port = 4433;
    unsigned workers = 1;
    bool webtransport_echo{};
    bool lifecycle_test{};
    bool outbound_pto_test{};
    std::string cold_cleanup_report;
    std::string restart_report;
    std::string active_restart_report;
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
        else if (std::string_view{argv[index]} == "--lifecycle-test")
        {
            lifecycle_test = true;
        }
        else if (std::string_view{argv[index]} == "--outbound-pto-test")
        {
            outbound_pto_test = true;
        }
        else if (std::string_view{argv[index]} == "--cold-cleanup-report" && index + 1 < argc)
        {
            cold_cleanup_report = argv[++index];
        }
        else if (std::string_view{argv[index]} == "--restart-report" && index + 1 < argc)
        {
            restart_report = argv[++index];
        }
        else if (std::string_view{argv[index]} == "--active-restart-report" && index + 1 < argc)
        {
            active_restart_report = argv[++index];
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

    auto server_observer = std::make_shared<cnetmod::http::v3::http3_server*>(nullptr);
    auto sampling_context = std::make_shared<cnetmod::io_context*>(nullptr);
    auto sampling_started = std::make_shared<std::atomic<bool>>();
    auto handler = [server_observer, sampling_context, sampling_started, cold_cleanup_report](cnetmod::http::v3::http3_request& request,
                       cnetmod::http::v3::http3_response& response) -> std::error_code
    {
        if (request.path == "/cold-cleanup-start" && !cold_cleanup_report.empty())
        {
            if (!sampling_started->exchange(true))
                cnetmod::spawn(**sampling_context, sample_cold_cleanup(
                    **sampling_context, **server_observer, cold_cleanup_report));
            response.status = 200;
            response.body = "ok\n";
            return {};
        }
        if (request.path == "/active-connections")
        {
            response.status = 200;
            response.body = std::to_string((*server_observer)->statistics().active_connections);
            return {};
        }
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
        [handler, outbound_pto_test, server_observer, sampling_context, active_restart_report](
            cnetmod::http::v3::http3_request& request,
            cnetmod::http::v3::http3_response& response,
            cnetmod::cancel_token& request_token) -> cnetmod::task<std::expected<void, std::error_code>>
    {
        if (outbound_pto_test && request.path == "/restart-inflight" && !active_restart_report.empty())
        {
            cnetmod::spawn(**sampling_context, restart_listener(
                **sampling_context, **server_observer, active_restart_report));
            const auto waited = co_await cnetmod::async_timer_wait(*cnetmod::io_context::current(),
                std::chrono::seconds{10}, request_token);
            co_return waited ? std::expected<void, std::error_code>{}
                : std::unexpected(waited.error());
        }
        if (outbound_pto_test && (request.path == "/delayed-pto" ||
            request.path == "/delayed-pto-full-frame"))
        {
            // Leave the listener with only its idle timer before the response
            // creates fresh recovery state from an asynchronous producer.
            (void)co_await cnetmod::async_timer_wait(*cnetmod::io_context::current(),
                std::chrono::milliseconds{1200});
            response.status = 200;
            // An empty header map and this body produce one 1166-byte H3
            // write: a 1170-byte STREAM frame in a 1199-byte QUIC datagram.
            response.body = request.path == "/delayed-pto-full-frame"
                ? std::string(1158U, 'x') : "delayed pto response\n";
            co_return {};
        }
        if (const auto error = handler(request, response); error)
            co_return std::unexpected(error);
        co_return {};
    };

    if (workers == 1U)
    {
        auto context = cnetmod::make_io_context();
        auto server = webtransport_echo || outbound_pto_test
            ? cnetmod::http::v3::make_http3_server(*context, tls,
                  cnetmod::endpoint{*address, port},
                  cnetmod::http::v3::http3_server_handlers{async_handler, webtransport_handler})
            : cnetmod::http::v3::make_http3_server(*context, tls,
                  cnetmod::endpoint{*address, port}, handler);
        *server_observer = server.get();
        *sampling_context = context.get();
        if (lifecycle_test)
        {
            (void)server->set_qpack_settings(65536U, 100U);
            (void)server->set_request_body_limits({.max_bytes = 1024U});
            (void)server->set_idle_timeout(std::chrono::milliseconds{500});
        }
        if (outbound_pto_test)
            (void)server->set_idle_timeout(std::chrono::milliseconds{3000});
        if (webtransport_echo)
            (void)server->set_max_datagram_frame_size(1200U);
        if (auto result = server->start(); !result)
        {
            logger::error{"server start failed: {}", result.error().message()};
            return 1;
        }
        logger::info{"HTTP/3 interop listener started on UDP {} with 1 worker", port};
        if (!restart_report.empty())
            cnetmod::spawn(*context, restart_listener(*context, *server, restart_report));
        context->run();
        return 0;
    }

    cnetmod::server_context context{workers, workers};
    auto server = webtransport_echo || outbound_pto_test
        ? cnetmod::http::v3::make_http3_server(context, tls,
              cnetmod::endpoint{*address, port}, cnetmod::http::v3::http3_server_handlers{std::move(async_handler), std::move(webtransport_handler)})
        : cnetmod::http::v3::make_http3_server(context, tls,
              cnetmod::endpoint{*address, port}, std::move(handler));
    *server_observer = server.get();
    *sampling_context = std::addressof(context.accept_io());
    if (lifecycle_test)
    {
        (void)server->set_qpack_settings(65536U, 100U);
        (void)server->set_request_body_limits({.max_bytes = 1024U});
        (void)server->set_idle_timeout(std::chrono::milliseconds{500});
    }
    if (outbound_pto_test)
        (void)server->set_idle_timeout(std::chrono::milliseconds{3000});
    if (webtransport_echo)
        (void)server->set_max_datagram_frame_size(1200U);
    if (auto result = server->start(); !result)
    {
        logger::error{"server start failed: {}", result.error().message()};
        return 1;
    }
    logger::info{"HTTP/3 interop listener started on UDP {} with {} workers", port,
        workers};
    if (!restart_report.empty())
        cnetmod::spawn(context.accept_io(), restart_listener(context.accept_io(), *server, restart_report));
    context.run();
    return 0;
}
