#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.executor.pool;
import cnetmod.protocol.http;

namespace cn = cnetmod;
namespace http = cnetmod::http;

namespace {

struct options
{
    std::uint16_t port{18080};
    unsigned workers{16};
    bool tls{};
    bool http2{};
    bool affinity{};
    bool minimal_headers{};
    std::string cert;
    std::string key;
};

auto parse_options(int argc, char** argv) -> std::optional<options>
{
    options result;
    for (int index = 1; index < argc; ++index)
    {
        const auto argument = std::string_view{argv[index]};
        const auto value = [&]() -> std::optional<std::string_view>
        {
            if (index + 1 >= argc)
                return std::nullopt;
            return std::string_view{argv[++index]};
        };
        if (argument == "--port")
        {
            const auto text = value();
            unsigned port{};
            if (!text)
                return std::nullopt;
            const auto [end, error] = std::from_chars(
                text->data(), text->data() + text->size(), port);
            if (error != std::errc{} || end != text->data() + text->size() ||
                port == 0U || port > 65535U)
                return std::nullopt;
            result.port = static_cast<std::uint16_t>(port);
        }
        else if (argument == "--workers")
        {
            const auto text = value();
            if (!text)
                return std::nullopt;
            const auto [end, error] = std::from_chars(
                text->data(), text->data() + text->size(), result.workers);
            if (error != std::errc{} || end != text->data() + text->size() ||
                result.workers == 0U)
                return std::nullopt;
        }
        else if (argument == "--tls")
            result.tls = true;
        else if (argument == "--http2")
            result.http2 = true;
        else if (argument == "--affinity")
            result.affinity = true;
        else if (argument == "--minimal-headers")
            result.minimal_headers = true;
        else if (argument == "--cert")
        {
            const auto text = value();
            if (!text)
                return std::nullopt;
            result.cert = *text;
        }
        else if (argument == "--key")
        {
            const auto text = value();
            if (!text)
                return std::nullopt;
            result.key = *text;
        }
        else
            return std::nullopt;
    }
    if (result.tls && (result.cert.empty() || result.key.empty()))
        return std::nullopt;
    return result;
}

} // namespace

auto main(int argc, char** argv) -> int
{
    const auto configuration = parse_options(argc, argv);
    if (!configuration)
    {
        std::println(stderr,
            "usage: crosslang_cnetmod_server --port N --workers N " "[--affinity] [--minimal-headers] [--http2] " "[--tls --cert FILE --key FILE]");
        return 2;
    }

    cn::net_init network;
    cn::thread_affinity_options affinity;
    if (configuration->affinity)
    {
        affinity.enabled = true;
        affinity.accept_processor = 0;
        affinity.worker_processors.reserve(configuration->workers);
        for (unsigned index = 0; index < configuration->workers; ++index)
            affinity.worker_processors.push_back(index);
    }
    cn::server_context context{configuration->workers,
        configuration->workers, std::move(affinity)};
    http::server server{context};
    if (configuration->minimal_headers)
        server.set_response_header_options(
            {.emit_server = false, .emit_date = false});
    http::router router;
    router.get("/hello", [minimal_headers = configuration->minimal_headers](http::request_context& request) -> cn::task<void>
        {
            if (minimal_headers)
            {
                request.resp().set_status(http::status::ok);
                request.resp().set_body(std::string_view{"Hello, World!"});
            }
            else
                request.text(http::status::ok, "Hello, World!");
            co_return;
        });
    server.set_router(std::move(router));

#ifdef CNETMOD_HAS_SSL
    std::optional<cn::ssl_context> tls_context;
    if (configuration->tls)
    {
        auto created = cn::ssl_context::server();
        if (!created)
            return 3;
        tls_context.emplace(std::move(*created));
        if (!tls_context->load_cert_file(configuration->cert) ||
            !tls_context->load_key_file(configuration->key))
            return 4;
        tls_context->configure_alpn_server(
            {configuration->http2 ? "h2" : "http/1.1"});
        server.set_ssl_context(*tls_context);
    }
#else
    if (configuration->tls)
    {
        std::println(stderr, "TLS requested, but this build has no SSL support");
        return 3;
    }
#endif

    if (!server.listen("127.0.0.1", configuration->port))
        return 5;
    cn::spawn(context.accept_io(), server.run());
    std::println("ready {} {}", configuration->port,
        configuration->http2 ? "http2" : "http1");
    context.run();
    return 0;
}
