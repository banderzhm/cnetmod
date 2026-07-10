#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cn = cnetmod;

auto run(cn::io_context& context, std::uint16_t port, bool verify_body, int& exit_code) -> cn::task<void> {
    cn::http::client_options options;
    options.version_pref = cn::http::http_version_preference::http2_only;
    options.keep_alive = true;
    options.enable_cookies = false;
    options.follow_redirects = false;
    options.verify_peer = false;
    cn::http::client client(context, options);

    auto first = co_await client.send(cn::http::http_method::GET,
        std::format("http://127.0.0.1:{}/hello", port));
    if (!first || first->status_code() != 200 || first->version() != cn::http::http_version::http_2 ||
        (verify_body && first->body() != "Hello, World!")) {
        exit_code = 1;
        context.stop();
        co_return;
    }
    auto second = co_await client.send(cn::http::http_method::POST,
        std::format("http://127.0.0.1:{}/native-client-post", port), "payload");
    if (!second || second->status_code() != 200 || second->version() != cn::http::http_version::http_2 ||
        (verify_body && second->body() != "received: payload"))
        exit_code = 1;
    std::vector<cn::http::request> batch;
    batch.reserve(16);
    for (unsigned index = 0; index < 16; ++index) {
        batch.emplace_back(cn::http::http_method::GET, "/hello");
    }
    const auto responses = co_await client.send_batch(batch);
    if (responses.size() != batch.size()) {
        exit_code = 1;
    }
    for (const auto& response : responses) {
        if (!response || response->status_code() != 200 ||
            (verify_body && response->body() != "Hello, World!")) {
            exit_code = 1;
        }
    }
    context.stop();
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    std::uint16_t port{};
    const auto text = std::string_view(argv[1]);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc{} || end != text.data() + text.size() || port == 0) return 2;
    cn::net_init network;
    auto context = cn::make_io_context();
    int exit_code{};
    cn::spawn(*context, run(*context, port, argc == 3 && std::string_view(argv[2]) == "--body", exit_code));
    context->run();
    return exit_code;
}
