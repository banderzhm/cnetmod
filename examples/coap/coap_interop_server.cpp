#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.core.net_init;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto make_text_response(const cn::coap::message& req, std::string_view body)
    -> cn::coap::message
{
    return cn::coap::text_response(req, body);
}

auto notify_loop(cn::io_context& ctx, std::shared_ptr<cn::coap::udp_server> server)
    -> cn::task<void>
{
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));
    for (int i = 1; i <= 3; ++i) {
        co_await cn::async_sleep(ctx, std::chrono::milliseconds(250));
        cn::coap::message notification;
        notification.set_response(cn::coap::response_code::content);
        notification.add_uint_option(cn::coap::option_number::content_format,
            static_cast<std::uint16_t>(cn::coap::content_format::text_plain));
        auto body = std::format("notify-{}", i);
        notification.payload.assign(reinterpret_cast<const std::byte*>(body.data()),
            reinterpret_cast<const std::byte*>(body.data() + body.size()));
        auto sent = co_await server->notify_observers("/sensors/temp", std::move(notification));
        std::println("COAP_NOTIFY_SENT {}", sent ? *sent : 0);
    }
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(250));
    server->stop();
    ctx.stop();
}

auto run_server(cn::io_context& ctx, std::uint16_t port) -> cn::task<void>
{
    auto server = std::make_shared<cn::coap::server>(ctx);
    auto listen = server->listen("127.0.0.1", port);
    if (!listen) {
        std::println("COAP_SERVER_ERROR {}", listen.error().message());
        ctx.stop();
        co_return;
    }

    server->set_etag_provider([](std::string_view path) -> std::optional<std::vector<std::byte>> {
        if (path == "/sensors/temp") {
            return cn::coap::to_bytes("temp-v1");
        }
        return std::nullopt;
    });

    server->route(cn::coap::method::get, "/sensors/temp",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            auto resp = make_text_response(req.request, "22.5");
            auto etag = cn::coap::to_bytes("temp-v1");
            resp.add_option(cn::coap::option_number::etag, etag);
            co_return resp;
        });
    server->register_resource(cn::coap::resource_description{
        .path = "/sensors/temp",
        .rt = "temperature-c",
        .if_ = "sensor",
        .observable = true,
    });

    server->route(cn::coap::method::get, "/large",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            std::string body;
            body.reserve(1800);
            for (int i = 0; i < 180; ++i) {
                body += std::format("{:02d}:cnetmod-blockwise;", i % 100);
            }
            co_return make_text_response(req.request, body);
        });
    server->register_resource(cn::coap::resource_description{
        .path = "/large",
        .rt = "large-text",
        .if_ = "block2",
    });

    server->route(cn::coap::method::post, "/upload",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            const auto body = std::format("upload-{}", req.request.payload.size());
            co_return make_text_response(req.request, body);
        });

    std::println("COAP_SERVER_READY {}", port);
    server->route(cn::coap::method::get, "/start-notify",
        [&ctx, server](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            cn::spawn(ctx, notify_loop(ctx, server));
            co_return make_text_response(req.request, "started");
        });
    co_await server->run();
}

auto main(int argc, char** argv) -> int
{
    cn::net_init net;
    const auto port = argc > 1
        ? static_cast<std::uint16_t>(std::stoul(argv[1]))
        : static_cast<std::uint16_t>(56830);
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_server(*ctx, port));
    ctx->run();
    return 0;
}
