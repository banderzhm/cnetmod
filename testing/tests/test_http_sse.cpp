#include "test_framework.hpp"
#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.io;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.recover;
import cnetmod.protocol.http.middleware.timeout;

using namespace cnetmod::http;

TEST(http1_stream_route_delivers_body_before_peer_finishes_request)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    server http_server{*io};
    router routes;
    bool first_chunk_seen = false;
    routes.stream_post("/upload",
        [&first_chunk_seen](request_context& request) -> cnetmod::task<void>
        {
            std::string body;
            while (auto chunk = co_await request.receive_body_chunk())
            {
                first_chunk_seen = true;
                body.append(reinterpret_cast<const char*>(chunk->data()),
                    chunk->size());
            }
            request.text(status::ok, body);
        },
        {.max_bytes = 64, .chunk_capacity = 1});
    http_server.set_router(std::move(routes));
    ASSERT_TRUE(http_server.listen("127.0.0.1", 0).has_value());
    const auto endpoint = http_server.local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    auto listener = http_server.run();
    listener.handle().resume();

    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*io, *peer, *endpoint))
                .has_value());
        const std::string first = "POST /upload HTTP/1.1\r\nHost: localhost\r\n" "Content-Length: 6\r\nConnection: close\r\n\r\nabc";
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                         cnetmod::const_buffer{first.data(), first.size()}))
                .has_value());
        for (int attempt = 0; attempt < 50 && !first_chunk_seen; ++attempt)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(first_chunk_seen);
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                         cnetmod::const_buffer{"def", 3}))
                .has_value());

        std::array<char, 4096> bytes{};
        auto read = co_await cnetmod::async_read(*io, *peer,
            cnetmod::mutable_buffer{bytes.data(), bytes.size()});
        ASSERT_TRUE(read.has_value() && *read > 0);
        ASSERT_TRUE((std::string_view{bytes.data(), *read}.contains("abcdef")));
        peer->close();
        http_server.stop();
        while (!listener.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        listener.handle().promise().result();
        while (http_server.active_connections() != 0)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

TEST(sse_encodes_named_multiline_event)
{
    auto frame = sse::encode(sse::event{
        .event = "update",
        .data = "line1\nline2",
        .id = "42",
        .retry = std::chrono::milliseconds{1500},
    });

    ASSERT_EQ(frame, std::string("id: 42\n" "event: update\n" "retry: 1500\n" "data: line1\n" "data: line2\n" "\n"));
}

TEST(sse_heartbeat_is_comment_frame)
{
    ASSERT_EQ(sse::heartbeat(), std::string(": keepalive\n\n"));
}

TEST(sse_prepare_sets_streaming_headers)
{
    response resp;
    resp.set_body(std::string_view("old"));
    sse::prepare(resp);

    ASSERT_EQ(resp.get_header("Content-Type"), std::string_view("text/event-stream; charset=utf-8"));
    ASSERT_EQ(resp.get_header("Cache-Control"), std::string_view("no-cache, no-transform"));
    ASSERT_EQ(resp.get_header("Connection"), std::string_view("keep-alive"));
    ASSERT_EQ(resp.get_header("X-Accel-Buffering"), std::string_view("no"));
    ASSERT_EQ(resp.get_header("X-Streamed"), std::string_view("1"));
    ASSERT_TRUE(resp.get_header("Content-Length").empty());
}

TEST(sse_make_response_omits_content_length)
{
    std::array events{
        sse::event{.event = "message", .data = "hello", .id = "1"},
        sse::event{.data = "world"},
    };

    auto resp = sse::make_response(std::span<const sse::event>{events.data(), events.size()});
    ASSERT_EQ(resp.status_code(), 200);
    ASSERT_TRUE(resp.get_header("Content-Length").empty());
    ASSERT_TRUE(resp.body().find("event: message\n") != std::string_view::npos);
    ASSERT_TRUE(resp.body().find("data: world\n\n") != std::string_view::npos);
}

TEST(sse_context_exposes_conservative_commit_state)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    auto client = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value() && client.has_value());
    ASSERT_TRUE(listener->bind(
                            {cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    bool verified = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE((co_await cnetmod::async_connect(
                         *io, *client, *endpoint))
                .has_value());
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        header_map headers;
        response outgoing;
        request_context request{*io, *accepted, "GET", "/events", headers,
            {}, outgoing, {}};

        router routes;
        routes.sse_get("/events",
            [](request_context&, sse_stream& stream) -> cnetmod::task<void>
            {
                ASSERT_FALSE(stream.started());
                ASSERT_TRUE(stream.state() == sse_stream_state::not_started);
                ASSERT_TRUE(co_await stream.heartbeat());
                ASSERT_TRUE(stream.started());
                ASSERT_TRUE(stream.state() == sse_stream_state::open);
                auto updates = stream.callback("update");
                ASSERT_TRUE(co_await updates(R"({"text":"ready"})"));
                ASSERT_TRUE(co_await stream.finish());
                ASSERT_TRUE(stream.state() == sse_stream_state::closed);
            });
        auto route = routes.match(http_method::GET, "/events");
        ASSERT_TRUE(route.has_value());
        if (!route)
        {
            io->stop();
            co_return;
        }
        co_await route->handler(request);

        std::string received;
        std::array<char, 4096> buffer{};
        while (!received.contains(": keepalive\n\n") ||
            !received.contains("event: update\ndata: {\"text\":\"ready\"}\n\n") ||
            !received.contains("data: {\"done\":true}\n\n") ||
            !received.contains("0\r\n\r\n"))
        {
            auto read = co_await cnetmod::async_read(*io, *client,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            ASSERT_TRUE(read.has_value() && *read > 0);
            if (!read || *read == 0)
                break;
            received.append(buffer.data(), *read);
        }
        verified = received.contains(": keepalive\n\n") &&
            received.contains("event: update\ndata: {\"text\":\"ready\"}\n\n") &&
            received.contains("data: {\"done\":true}\n\n") &&
            received.contains("Transfer-Encoding: chunked\r\n") &&
            received.ends_with("0\r\n\r\n");
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(verified);
}

TEST(sse_finish_terminates_response_and_preserves_http1_keep_alive)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    server http_server{*io};
    router routes;
    routes.sse_get("/events",
        [](request_context&, sse_stream& stream) -> cnetmod::task<void>
        {
            ASSERT_TRUE(co_await stream.send("ready", "update"));
            ASSERT_TRUE(co_await stream.finish());
        });
    routes.get("/health",
        [](request_context& request) -> cnetmod::task<void>
        {
            request.text(status::ok, "ok");
            co_return;
        });
    http_server.set_router(std::move(routes));
    ASSERT_TRUE(http_server.listen("127.0.0.1", 0).has_value());
    const auto endpoint = http_server.local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    auto listener = http_server.run();
    listener.handle().resume();

    bool verified = false;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*io, *peer, *endpoint))
                .has_value());

        constexpr std::string_view first =
            "GET /events HTTP/1.1\r\nHost: localhost\r\n\r\n";
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                         cnetmod::const_buffer{first.data(), first.size()}))
                .has_value());

        std::string streamed;
        std::array<char, 4096> bytes{};
        while (!streamed.contains("0\r\n\r\n"))
        {
            const auto read = co_await cnetmod::async_read(*io, *peer,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(read.has_value() && *read > 0);
            if (!read || *read == 0)
                break;
            streamed.append(bytes.data(), *read);
        }

        constexpr std::string_view second =
            "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                         cnetmod::const_buffer{second.data(), second.size()}))
                .has_value());
        const auto read = co_await cnetmod::async_read(*io, *peer,
            cnetmod::mutable_buffer{bytes.data(), bytes.size()});
        ASSERT_TRUE(read.has_value() && *read > 0);
        const std::string_view health{bytes.data(), read ? *read : 0};
        verified = streamed.contains("Transfer-Encoding: chunked\r\n") &&
            streamed.contains("event: update\n") &&
            streamed.contains("data: ready\n\n") &&
            streamed.contains("data: {\"done\":true}\n\n") &&
            streamed.ends_with("0\r\n\r\n") &&
            health.contains("HTTP/1.1 200 OK") && health.ends_with("ok");

        peer->close();
        http_server.stop();
        while (!listener.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        listener.handle().promise().result();
        while (http_server.active_connections() != 0)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
    ASSERT_TRUE(verified);
}

#ifdef CNETMOD_HAS_SSL
TEST(sse_finish_uses_tls_transport_and_preserves_http1_keep_alive)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto server_tls = cnetmod::ssl_context::server();
    ASSERT_TRUE(server_tls.has_value());
    ASSERT_TRUE(server_tls->load_cert_file(CNETMOD_HTTP_SSE_TEST_CERT)
            .has_value());
    ASSERT_TRUE(server_tls->load_key_file(CNETMOD_HTTP_SSE_TEST_KEY)
            .has_value());

    server http_server{*io};
    router routes;
    routes.sse_get("/events",
        [](request_context&, sse_stream& stream) -> cnetmod::task<void>
        {
            ASSERT_TRUE(co_await stream.send("secure", "update"));
            ASSERT_TRUE(co_await stream.finish());
        });
    routes.get("/health",
        [](request_context& request) -> cnetmod::task<void>
        {
            request.text(status::ok, "ok");
            co_return;
        });
    http_server.set_router(std::move(routes));
    http_server.set_ssl_context(*server_tls);
    ASSERT_TRUE(http_server.listen("127.0.0.1", 0).has_value());
    const auto endpoint = http_server.local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    auto listener = http_server.run();
    listener.handle().resume();

    bool verified = false;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto client_tls = cnetmod::ssl_context::client();
        ASSERT_TRUE(client_tls.has_value());
        client_tls->set_verify_peer(false);
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*io, *peer, *endpoint))
                .has_value());
        cnetmod::ssl_stream stream{*client_tls, *io, *peer};
        stream.set_connect_state();
        ASSERT_TRUE((co_await stream.async_handshake()).has_value());

        constexpr std::string_view first =
            "GET /events HTTP/1.1\r\nHost: localhost\r\n\r\n";
        ASSERT_TRUE((co_await stream.async_write_all(
                         cnetmod::const_buffer{first.data(), first.size()}))
                .has_value());

        std::string streamed;
        std::array<char, 4096> bytes{};
        while (!streamed.contains("0\r\n\r\n"))
        {
            const auto read = co_await stream.async_read(
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(read.has_value() && *read > 0);
            if (!read || *read == 0)
                break;
            streamed.append(bytes.data(), *read);
        }

        constexpr std::string_view second =
            "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        ASSERT_TRUE((co_await stream.async_write_all(
                         cnetmod::const_buffer{second.data(), second.size()}))
                .has_value());
        const auto read = co_await stream.async_read(
            cnetmod::mutable_buffer{bytes.data(), bytes.size()});
        ASSERT_TRUE(read.has_value() && *read > 0);
        const std::string_view health{bytes.data(), read ? *read : 0};
        verified = streamed.contains("Transfer-Encoding: chunked\r\n") &&
            streamed.contains("event: update\n") &&
            streamed.contains("data: secure\n\n") &&
            streamed.contains("data: {\"done\":true}\n\n") &&
            streamed.ends_with("0\r\n\r\n") &&
            health.contains("HTTP/1.1 200 OK") && health.ends_with("ok");

        peer->close();
        http_server.stop();
        while (!listener.handle().done())
            co_await cnetmod::async_sleep(
                *io, std::chrono::milliseconds{1});
        listener.handle().promise().result();
        while (http_server.active_connections() != 0)
            co_await cnetmod::async_sleep(
                *io, std::chrono::milliseconds{1});
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
    ASSERT_TRUE(verified);
}
#endif

TEST(sse_router_registers_get_and_post_endpoints)
{
    router routes;
    auto handler = [](request_context&, sse_stream&) -> cnetmod::task<void>
    {
        co_return;
    };
    routes.sse_get("/events", handler).sse_post("/chat", handler);

    ASSERT_TRUE(routes.match(http_method::GET, "/events").has_value());
    ASSERT_FALSE(routes.match(http_method::POST, "/events").has_value());
    ASSERT_TRUE(routes.match(http_method::POST, "/chat").has_value());
    ASSERT_FALSE(routes.match(http_method::GET, "/chat").has_value());
}

TEST(sse_request_lifecycle_allows_validation_before_dynamic_activation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    header_map headers;
    bool expired = false;

    router routes;
    routes.get("/events",
        [&expired](request_context& request) -> cnetmod::task<void>
        {
            if (request.query_string() != "stream=true")
            {
                request.text(status::bad_request, "streaming not requested");
                co_return;
            }
            co_await request.with_sse(
                [&expired](request_context& context,
                    sse_stream& stream) -> cnetmod::task<void>
                {
                    co_await cnetmod::async_sleep(context.io_ctx(),
                        std::chrono::milliseconds{5});
                    expired = !(co_await stream.send("late")) &&
                        stream.state() == sse_stream_state::failed &&
                        context.request_deadline().expired();
                },
                {.max_duration = std::chrono::milliseconds{1},
                    .write_timeout = std::chrono::milliseconds{1}});
        });
    const auto route = routes.match(http_method::GET, "/events");
    ASSERT_TRUE(route.has_value());

    auto run = [&]() -> cnetmod::task<void>
    {
        response rejected_response;
        request_context rejected{*io, peer, "GET",
            "/events?stream=false", headers, {}, rejected_response, {}};
        co_await route->handler(rejected);
        ASSERT_EQ(rejected_response.status_code(), status::bad_request);
        ASSERT_FALSE(rejected.sse_started());

        response streaming_response;
        request_context streaming{*io, peer, "GET",
            "/events?stream=true", headers, {}, streaming_response, {}};
        co_await route->handler(streaming);
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(expired);
}

TEST(sse_route_enforces_maximum_duration_before_writing)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    header_map headers;
    response outgoing;
    request_context request{*io, peer, "GET", "/events", headers, {},
        outgoing, {}};
    bool expired = false;
    router routes;
    routes.sse_get("/events", [&expired](request_context& context, sse_stream& stream) -> cnetmod::task<void>
        {
            co_await cnetmod::async_sleep(context.io_ctx(),
                std::chrono::milliseconds{5});
            expired = !(co_await stream.send("late")) &&
                stream.state() == sse_stream_state::failed;
        },
        sse_stream_options{
            .max_duration = std::chrono::milliseconds{1},
            .write_timeout = std::chrono::milliseconds{1},
        });
    const auto route = routes.match(http_method::GET, "/events");
    ASSERT_TRUE(route.has_value());
    auto run = [&]() -> cnetmod::task<void>
    {
        co_await route->handler(request);
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(expired);

    bool rejected = false;
    try
    {
        routes.sse_defaults({.max_duration = std::chrono::milliseconds{0}});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    ASSERT_TRUE(rejected);
}

TEST(sse_recovery_finishes_committed_stream_without_http_fallback)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    auto client = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value() && client.has_value());
    ASSERT_TRUE(listener->bind(
                            {cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    bool verified = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE((co_await cnetmod::async_connect(
                         *io, *client, *endpoint))
                .has_value());
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        header_map headers;
        response outgoing;
        request_context request{*io, *accepted, "GET", "/events", headers,
            {}, outgoing, {}};

        router routes;
        routes.sse_get("/events",
            [](request_context& context,
                sse_stream& stream) -> cnetmod::task<void>
            {
                ASSERT_TRUE(co_await stream.send(R"({"text":"partial"})", "delta"));
                co_await cnetmod::async_sleep(context.io_ctx(),
                    std::chrono::milliseconds{5});
                throw std::runtime_error{"stream producer failed"};
            });
        auto route = routes.match(http_method::GET, "/events");
        ASSERT_TRUE(route.has_value());
        if (!route)
        {
            io->stop();
            co_return;
        }
        auto recovery = cnetmod::recover({.log_body = false,
            .allow_env_override = false});
        auto timeout = cnetmod::request_timeout(std::chrono::milliseconds{1});
        co_await timeout(request,
            [&request, recovery = std::move(recovery),
                handler = route->handler]() mutable -> cnetmod::task<void>
            {
                co_await recovery(request,
                    [&request, handler = std::move(handler)]() mutable
                        -> cnetmod::task<void>
                    {
                        co_await handler(request);
                    });
            });

        std::string received;
        std::array<char, 4096> buffer{};
        while (!received.contains("data: {\"done\":true}\n\n"))
        {
            auto read = co_await cnetmod::async_read(*io, *client,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            ASSERT_TRUE(read.has_value() && *read > 0);
            if (!read || *read == 0)
                break;
            received.append(buffer.data(), *read);
        }
        verified = received.contains(
                       "event: delta\ndata: {\"text\":\"partial\"}\n\n") &&
            received.contains("event: error\n") &&
            received.contains("\"error\":\"internal server error\"") &&
            received.contains("data: {\"done\":true}\n\n") &&
            !received.contains("HTTP/1.1 500");
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(verified);
}

RUN_TESTS()
