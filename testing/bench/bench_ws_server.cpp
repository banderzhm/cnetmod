#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.websocket;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cn = cnetmod;
namespace ws = cn::ws;

namespace {
auto echo(ws::ws_context& ctx) -> cn::task<void>
{
    while (ctx.is_open())
    {
        auto msg = co_await ctx.recv();
        if (!msg || msg->op == ws::opcode::close)
            break;
        auto result = msg->op == ws::opcode::binary
            ? co_await ctx.send_binary(msg->payload)
            : co_await ctx.send_text(msg->as_string());
        if (!result)
            break;
    }
}

template <typename T>
auto number(const char* text, T& result) -> bool
{
    auto value = std::string_view{text};
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    return error == std::errc{} && end == value.data() + value.size() && result > 0;
}
} // namespace

auto main(int argc, char** argv) -> int
{
    cn::net_init net;
    std::uint16_t port = 19090;
    unsigned workers = 1;
    if ((argc != 3 && argc != 5) || !number(argv[1], port) || !number(argv[2], workers))
    {
        logger::error{"usage: bench_ws_server PORT WORKERS [CERT KEY]"};
        logger::shutdown();
        return 2;
    }
    cn::server_context context(workers, 1);
    ws::server server(context);
#ifdef CNETMOD_HAS_SSL
    std::optional<cn::ssl_context> tls;
    if (argc == 5)
    {
        auto result = cn::ssl_context::server();
        if (!result)
            return 1;
        tls.emplace(std::move(*result));
        if (!tls->load_cert_file(argv[3]) || !tls->load_key_file(argv[4]))
            return 1;
        server.set_ssl_context(*tls);
    }
#else
    if (argc == 5)
        return 2;
#endif
    server.on("/ws", echo);
    if (auto result = server.listen("127.0.0.1", port); !result)
    {
        logger::error{"listen failed: {}", result.error().message()};
        logger::shutdown();
        return 1;
    }
    logger::info{"WebSocket echo ready port={} workers={} tls={} compression=off",
        port, workers, argc == 5};
    cn::spawn(context.accept_io(), server.run());
    context.run();
    logger::shutdown();
}
