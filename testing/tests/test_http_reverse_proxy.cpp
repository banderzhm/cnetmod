#include "test_framework.hpp"

import std;
import cnetmod.core.net_init;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.http.reverse_proxy;

namespace {

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

} // namespace

auto main() -> int
{
    return cnetmod::test::run_all();
}
