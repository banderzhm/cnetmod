#include "test_framework.hpp"

#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.http.reverse_proxy;

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
import cnetmod.protocol.http.v3.session;
#endif

namespace {

TEST(http_client_reconnects_after_http10_default_close)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    bool backend_valid = true;
    auto backend = [&]() -> cnetmod::task<void>
    {
        for (unsigned request_index = 0; request_index < 2U; ++request_index)
        {
            auto peer = co_await cnetmod::async_accept(*context, *listener);
            if (!peer)
            {
                backend_valid = false;
                co_return;
            }
            std::array<char, 1024> bytes{};
            auto received = co_await cnetmod::async_read(*context, *peer,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            backend_valid = backend_valid && received && *received != 0U &&
                std::string_view{bytes.data(), *received}.contains("GET / HTTP/1.1");
            constexpr std::string_view reply =
                "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nok";
            const auto written = co_await cnetmod::async_write_all(*context, *peer,
                cnetmod::const_buffer{reply.data(), reply.size()});
            backend_valid = backend_valid && written.has_value();
            peer->close();
        }
    };

    bool client_valid = false;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::client_options options;
        options.version_pref =
            cnetmod::http::http_version_preference::http1_only;
        cnetmod::http::client client{*context, std::move(options)};
        const auto target = std::format("http://127.0.0.1:{}/",
            endpoint->port());
        cnetmod::http::request first{cnetmod::http::http_method::GET, target};
        cnetmod::http::request second{cnetmod::http::http_method::GET, target};
        const auto first_result = co_await client.send(first);
        const auto second_result = co_await client.send(second);
        client_valid = first_result && second_result &&
            first_result->body() == "ok" && second_result->body() == "ok";
        context->stop();
    };

    cnetmod::spawn(*context, backend());
    cnetmod::spawn(*context, exercise());
    context->run();
    ASSERT_TRUE(backend_valid);
    ASSERT_TRUE(client_valid);
}

TEST(reverse_proxy_rejects_invalid_upstream_origins)
{
    auto context = cnetmod::make_io_context();
    auto missing_scheme = cnetmod::http::reverse_proxy::create(*context,
        {.upstream = "127.0.0.1:8080"});
    auto unsupported_scheme = cnetmod::http::reverse_proxy::create(*context,
        {.upstream = "ftp://127.0.0.1/resource"});
    auto query_in_origin = cnetmod::http::reverse_proxy::create(*context,
        {.upstream = "http://127.0.0.1?fixed=true"});

    ASSERT_FALSE(missing_scheme.has_value());
    ASSERT_FALSE(unsupported_scheme.has_value());
    ASSERT_FALSE(query_in_origin.has_value());
}

TEST(reverse_proxy_forwards_request_and_filters_hop_headers)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();

    bool backend_received = false;
    bool backend_headers_valid = false;
    cnetmod::http::server backend{*context};
    cnetmod::http::router backend_routes;
    backend_routes.post("/proxy",
        [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            backend_received = request.body() == "payload";
            backend_headers_valid =
                request.get_header("Host").starts_with("127.0.0.1:") &&
                request.get_header("X-Hop").empty() &&
                request.get_header("X-Forwarded-For") == "127.0.0.1" &&
                request.get_header("X-Forwarded-Host").starts_with("127.0.0.1:") &&
                request.get_header("X-Forwarded-Proto") == "http";
            request.resp().set_status(201);
            request.resp().set_header("X-Upstream", "yes");
            request.resp().set_header("Connection", "close");
            request.resp().set_body(std::string{"forwarded:"} +
                std::string{request.body()});
            co_return;
        });
    backend_routes.get("/slow",
        [event_loop = context.get()](cnetmod::http::request_context& request)
            -> cnetmod::task<void>
        {
            co_await cnetmod::async_sleep(
                *event_loop, std::chrono::milliseconds{100});
            request.text(cnetmod::http::status::ok, "too late");
        });
    backend.set_router(std::move(backend_routes));
    ASSERT_TRUE(backend.listen("127.0.0.1", 0).has_value());
    const auto backend_endpoint = backend.local_endpoint();
    ASSERT_TRUE(backend_endpoint.has_value());

    cnetmod::http::reverse_proxy_options proxy_options;
    proxy_options.upstream = std::format("http://127.0.0.1:{}",
        backend_endpoint->port());
    proxy_options.downstream_scheme = "http";
    proxy_options.pool.maximum_clients = 2;
    proxy_options.pool.maximum_waiters = 2;
    proxy_options.pool.client.version_pref =
        cnetmod::http::http_version_preference::http1_only;
    proxy_options.pool.client.request_timeout =
        std::chrono::milliseconds{20};
    auto proxy = cnetmod::http::reverse_proxy::create(
        *context, std::move(proxy_options));
    ASSERT_TRUE(proxy.has_value());

    cnetmod::http::server frontend{*context};
    cnetmod::http::router frontend_routes;
    frontend_routes.post("/proxy",
        [&proxy](cnetmod::http::request_context& request)
            -> cnetmod::task<void>
        {
            co_await (*proxy)->forward(request);
        });
    frontend_routes.get("/slow",
        [&proxy](cnetmod::http::request_context& request)
            -> cnetmod::task<void>
        {
            co_await (*proxy)->forward(request);
        });
    frontend.set_router(std::move(frontend_routes));
    ASSERT_TRUE(frontend.listen("127.0.0.1", 0).has_value());
    const auto frontend_endpoint = frontend.local_endpoint();
    ASSERT_TRUE(frontend_endpoint.has_value());

    auto backend_loop = backend.run();
    auto frontend_loop = frontend.run();
    backend_loop.handle().resume();
    frontend_loop.handle().resume();

    bool response_valid = false;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::client_options client_options;
        client_options.version_pref =
            cnetmod::http::http_version_preference::http1_only;
        cnetmod::http::client client{*context, std::move(client_options)};
        cnetmod::http::request request{cnetmod::http::http_method::POST,
            std::format("http://127.0.0.1:{}/proxy",
                frontend_endpoint->port())};
        request.set_header("Connection", "keep-alive, X-Hop");
        request.set_header("X-Hop", "must-not-forward");
        request.set_header("X-Forwarded-For", "198.51.100.7");
        request.set_body(std::string_view{"payload"});

        auto result = co_await client.send(request);
        response_valid = result && result->status_code() == 201 &&
            result->body() == "forwarded:payload" &&
            result->get_header("X-Upstream") == "yes";
        cnetmod::http::request slow_request{cnetmod::http::http_method::GET,
            std::format("http://127.0.0.1:{}/slow",
                frontend_endpoint->port())};
        auto timed_out = co_await client.send(slow_request);
        response_valid = response_valid && timed_out &&
            timed_out->status_code() == cnetmod::http::status::gateway_timeout;
        client.close();
        co_await (*proxy)->close();
        frontend.stop();
        backend.stop();
        frontend.abort_connections();
        backend.abort_connections();
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{2};
        while ((!frontend_loop.handle().done() ||
                   !backend_loop.handle().done() ||
                   frontend.active_connections() != 0 ||
                   backend.active_connections() != 0) &&
            std::chrono::steady_clock::now() < deadline)
            co_await cnetmod::async_sleep(
                *context, std::chrono::milliseconds{1});
        context->stop();
    };

    auto running = exercise();
    running.handle().resume();
    context->run();
    ASSERT_TRUE(running.handle().done());
    running.handle().promise().result();

    ASSERT_TRUE(backend_received);
    ASSERT_TRUE(backend_headers_valid);
    ASSERT_TRUE(response_valid);
    ASSERT_TRUE(frontend_loop.handle().done());
    ASSERT_TRUE(backend_loop.handle().done());
}

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
TEST(reverse_proxy_bridges_http3_request_to_http_upstream)
{
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();

    bool backend_valid = false;
    cnetmod::http::server backend{*context};
    cnetmod::http::router routes;
    routes.post("/from-h3",
        [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            backend_valid = request.body() == "h3 payload" &&
                request.get_header("X-Hop").empty() &&
                request.get_header("X-Forwarded-For").empty() &&
                request.get_header("X-Forwarded-Host") == "gateway.test" &&
                request.get_header("X-Forwarded-Proto") == "https";
            request.resp().set_status(202);
            request.resp().set_header("X-Upstream", "h3-bridge");
            request.resp().set_body(std::string_view{"bridged"});
            co_return;
        });
    backend.set_router(std::move(routes));
    ASSERT_TRUE(backend.listen("127.0.0.1", 0).has_value());
    const auto endpoint = backend.local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    cnetmod::http::reverse_proxy_options options;
    options.upstream =
        std::format("http://127.0.0.1:{}", endpoint->port());
    options.downstream_scheme = "https";
    options.pool.client.version_pref =
        cnetmod::http::http_version_preference::http1_only;
    auto proxy = cnetmod::http::reverse_proxy::create(
        *context, std::move(options));
    ASSERT_TRUE(proxy.has_value());

    auto backend_loop = backend.run();
    backend_loop.handle().resume();
    cnetmod::http::v3::http3_response response;
    bool forwarded = false;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::v3::http3_request request;
        request.method = cnetmod::http::http_method::POST;
        request.path = "/from-h3";
        request.scheme = "https";
        request.host = "gateway.test";
        request.headers["Connection"] = "X-Hop";
        request.headers["X-Hop"] = "must-not-forward";
        request.headers["X-Forwarded-For"] = "198.51.100.9";
        request.body = "h3 payload";
        cnetmod::cancel_token cancellation;
        const auto result =
            co_await (*proxy)->forward(request, response, cancellation);
        forwarded = result.has_value();

        co_await (*proxy)->close();
        backend.stop();
        backend.abort_connections();
        while (!backend_loop.handle().done() ||
            backend.active_connections() != 0)
            co_await cnetmod::async_sleep(
                *context, std::chrono::milliseconds{1});
        context->stop();
    };

    auto running = exercise();
    running.handle().resume();
    context->run();
    ASSERT_TRUE(running.handle().done());
    running.handle().promise().result();

    ASSERT_TRUE(forwarded);
    ASSERT_TRUE(backend_valid);
    ASSERT_EQ(response.status, 202);
    ASSERT_EQ(response.body, "bridged");
    ASSERT_EQ(response.headers["x-upstream"], "h3-bridge");
    for (const auto& [name, value] : response.headers)
    {
        (void)value;
        ASSERT_TRUE(std::ranges::none_of(name,
            [](char character)
            {
                return character >= 'A' && character <= 'Z';
            }));
    }
}
#endif

} // namespace

auto main() -> int
{
    return cnetmod::test::run_all();
}
