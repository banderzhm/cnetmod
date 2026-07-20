#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.protocol.http;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::uint16_t port{};
    const auto text = std::string_view(argv[1]);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc{} || end != text.data() + text.size() || port == 0) return 2;

    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    cnetmod::http::server server(*context);
    if (const auto listen = server.listen("127.0.0.1", port); !listen) return 1;
    cnetmod::http::router router;
    router.get("/hello", [](cnetmod::http::request_context& request) -> cnetmod::task<void> {
        request.text(cnetmod::http::status::ok, "Hello, World!");
        co_return;
    });
    router.post("/native-client-post", [](cnetmod::http::request_context& request) -> cnetmod::task<void> {
        request.text(cnetmod::http::status::ok, "received: " + std::string(request.body()));
        co_return;
    });
    router.any("/*path", [](cnetmod::http::request_context& request) -> cnetmod::task<void> {
        if (request.method() == "POST") {
            request.text(cnetmod::http::status::ok, "received: " + std::string(request.body()));
        } else {
            request.text(cnetmod::http::status::ok, "Hello, World!");
        }
        co_return;
    });
    server.set_router(std::move(router));
    std::println("READY {}", port);
    std::fflush(stdout);
    cnetmod::spawn(*context, server.run());
    context->run();
}
