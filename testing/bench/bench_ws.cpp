/// cnetmod benchmark - WebSocket echo throughput over WS and WSS.
///
/// The benchmark uses cnetmod::ws::client and cnetmod::ws::server so the
/// measured path covers WebSocket handshake, masking, frame codec, TLS for WSS,
/// coroutine scheduling, and full echo round-trips.

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

#include "bench_framework.hpp"

namespace cn = cnetmod;
namespace ws = cnetmod::ws;

using cnetmod::bench::format_number;

namespace {

struct bench_case {
    const char* name;
    bool tls;
    std::uint16_t port_base;
};

constexpr bench_case cases[] = {
    {"WS echo",  false, 19480},
    {"WSS echo", true,  19580},
};

auto make_cert_path(std::string_view file) -> std::string {
    auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    return (root / "examples" / "test_ssl" / file).string();
}

auto env_u32(const char* name, unsigned fallback) -> unsigned {
    auto* value = std::getenv(name);
    if (value == nullptr) return fallback;
    unsigned out = 0;
    auto text = std::string_view(value);
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    if (ec != std::errc{} || ptr != text.data() + text.size() || out == 0) {
        return fallback;
    }
    return out;
}

auto default_worker_count(std::size_t client_count) -> unsigned {
    auto hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    auto local_fair = std::max(1u, hw / 2);
    return std::max(1u, std::min<unsigned>(local_fair, static_cast<unsigned>(client_count)));
}

auto echo_handler(ws::ws_context& ctx) -> cn::task<void> {
    while (ctx.is_open()) {
        auto msg = co_await ctx.recv();
        if (!msg || msg->op == ws::opcode::close) break;

        std::string_view payload{
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size(),
        };
        auto wr = co_await ctx.send_text(payload);
        if (!wr) break;
    }
}

auto run_ws_client(cn::io_context& ctx,
                   const bench_case& cfg,
                   std::string url,
                   std::size_t messages) -> cn::task<std::size_t>
{
    ws::client client(ctx);
    ws::client_options opts;
#ifdef CNETMOD_HAS_SSL
    opts.connect.tls_verify = false;
#endif

    auto cr = co_await client.connect(url, opts);
    if (!cr) co_return 0;

    static constexpr std::string_view payload = "0123456789abcdef";
    std::size_t completed = 0;
    for (std::size_t i = 0; i < messages; ++i) {
        auto sr = co_await client.send_text(payload);
        if (!sr) break;

        auto msg = co_await client.recv();
        if (!msg || msg->op != ws::opcode::text) break;
        auto body = std::string_view{
            reinterpret_cast<const char*>(msg->payload.data()),
            msg->payload.size(),
        };
        if (body != payload) break;
        ++completed;
    }

    (void)co_await client.close();
    co_return completed;
}

auto run_bench_client(cn::io_context& ctx,
                      bench_case cfg,
                      std::string url,
                      std::size_t messages_per_client,
                      std::size_t client_count,
                      std::atomic<std::size_t>& total,
                      std::atomic<std::size_t>& done,
                      ws::server& srv,
                      cn::server_context& server_ctx,
                      std::vector<cn::io_context*>& client_contexts) -> cn::task<void>
{
    auto n = co_await run_ws_client(ctx, cfg, std::move(url), messages_per_client);
    total.fetch_add(n, std::memory_order_relaxed);
    if (done.fetch_add(1, std::memory_order_acq_rel) + 1 >= client_count) {
        srv.stop();
        server_ctx.stop();
        for (auto* client_ctx : client_contexts) {
            client_ctx->stop();
        }
    }
}

void run_ws_bench(const bench_case& cfg,
                  std::size_t messages_per_client,
                  std::size_t client_count)
{
    auto fallback_workers = default_worker_count(client_count);
    auto server_workers = env_u32("CNETMOD_BENCH_SERVER_WORKERS", fallback_workers);
    auto client_workers = env_u32("CNETMOD_BENCH_CLIENT_WORKERS", fallback_workers);
    client_workers = std::max(1u, std::min<unsigned>(client_workers, static_cast<unsigned>(client_count)));

    cn::server_context sctx(server_workers, server_workers);
    ws::server srv(sctx);
    srv.on("/echo", echo_handler);

#ifdef CNETMOD_HAS_SSL
    std::optional<cn::ssl_context> ssl_ctx;
    if (cfg.tls) {
        auto ssl_ctx_r = cn::ssl_context::server();
        if (!ssl_ctx_r) {
            logger::error("  {:<10}: ssl_context::server failed: {}",
                          cfg.name, ssl_ctx_r.error().message());
            return;
        }
        ssl_ctx.emplace(std::move(*ssl_ctx_r));
        auto cert = make_cert_path("cert.pem");
        auto key = make_cert_path("key.pem");
        if (auto r = ssl_ctx->load_cert_file(cert); !r) {
            logger::error("  {:<10}: load_cert_file failed: {}", cfg.name, r.error().message());
            return;
        }
        if (auto r = ssl_ctx->load_key_file(key); !r) {
            logger::error("  {:<10}: load_key_file failed: {}", cfg.name, r.error().message());
            return;
        }
        srv.set_ssl_context(*ssl_ctx);
    }
#else
    if (cfg.tls) {
        logger::info("  {:<10}{:>14} msg/sec  (skipped: SSL disabled)", cfg.name, "-");
        return;
    }
#endif

    auto port = cfg.port_base;
    bool listened = false;
    for (int attempt = 0; attempt < 100; ++attempt, ++port) {
        if (srv.listen("127.0.0.1", port)) {
            listened = true;
            break;
        }
    }
    if (!listened) {
        logger::error("  {:<10}: listen failed", cfg.name);
        return;
    }

    std::vector<std::unique_ptr<cn::io_context>> client_ios;
    std::vector<cn::io_context*> client_contexts;
    client_ios.reserve(client_workers);
    client_contexts.reserve(client_workers);
    for (unsigned i = 0; i < client_workers; ++i) {
        client_ios.push_back(cn::make_io_context());
        client_contexts.push_back(client_ios.back().get());
    }

    const auto scheme = cfg.tls ? "wss" : "ws";
    const auto url = std::format("{}://127.0.0.1:{}/echo", scheme, port);
    std::atomic<std::size_t> total{0};
    std::atomic<std::size_t> done{0};

    cn::spawn(sctx.accept_io(), srv.run());
    std::jthread server_thread([&sctx] {
        sctx.run();
    });

    const auto started = std::chrono::steady_clock::now();
    std::vector<std::jthread> client_threads;
    client_threads.reserve(client_contexts.size());
    for (std::size_t i = 0; i < client_count; ++i) {
        auto& client_ctx = *client_contexts[i % client_contexts.size()];
        cn::spawn(client_ctx, run_bench_client(client_ctx,
                                              cfg,
                                              url,
                                              messages_per_client,
                                              client_count,
                                              total,
                                              done,
                                              srv,
                                              sctx,
                                              client_contexts));
    }
    for (auto* client_ctx : client_contexts) {
        client_threads.emplace_back([client_ctx] {
            client_ctx->run();
        });
    }
    for (auto& t : client_threads) {
        if (t.joinable()) t.join();
    }
    sctx.stop();
    if (server_thread.joinable()) server_thread.join();

    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto ok = total.load(std::memory_order_relaxed);
    const auto expected = messages_per_client * client_count;
    const double mps = elapsed > 0.0 ? static_cast<double>(ok) / elapsed : 0.0;

    logger::info("  {:<10} mc:{:<2}/{:<2} {:>4} x {:<7}{:>14} msg/sec  ({} ok, {} failed)",
                 cfg.name,
                 server_workers,
                 client_workers,
                 client_count,
                 messages_per_client,
                 format_number(mps),
                 ok,
                 expected - ok);
}

void run_matrix(std::size_t messages_per_client, std::size_t client_count) {
    for (auto cfg : cases) {
        run_ws_bench(cfg, messages_per_client, client_count);
    }
}

} // namespace

int main(int argc, char** argv)
{
    cn::net_init net;

    logger::info("================================================================");
    logger::info("  cnetmod WebSocket Benchmark");
    logger::info("================================================================");
#ifdef _WIN32
    logger::info("  Platform  : Windows IOCP");
#elif defined(__APPLE__)
    logger::info("  Platform  : macOS kqueue");
#else
    logger::info("  Platform  : Linux epoll/io_uring");
#endif
#ifdef NDEBUG
    logger::info("  Build     : Release");
#else
    logger::info("  Build     : Debug (results not representative!)");
#endif
    logger::info("  Payload   : 16-byte text echo round-trip");
    logger::info("  Matrix    : WS, WSS");
    logger::info("  Mode      : multicore; env CNETMOD_BENCH_*_WORKERS overrides");
    logger::info("================================================================");

    if (argc == 3) {
        std::size_t messages = 0;
        std::size_t clients = 0;
        auto msg_text = std::string_view(argv[1]);
        auto client_text = std::string_view(argv[2]);
        auto [msg_ptr, msg_ec] = std::from_chars(
            msg_text.data(), msg_text.data() + msg_text.size(), messages);
        auto [client_ptr, client_ec] = std::from_chars(
            client_text.data(), client_text.data() + client_text.size(), clients);
        if (msg_ec != std::errc{} || client_ec != std::errc{} ||
            msg_ptr != msg_text.data() + msg_text.size() ||
            client_ptr != client_text.data() + client_text.size() ||
            messages == 0 || clients == 0) {
            std::println("usage: bench_ws [messages_per_client clients]");
            logger::shutdown();
            return 2;
        }
        run_matrix(messages, clients);
        logger::info("================================================================");
        logger::shutdown();
        return 0;
    }

    run_matrix(1'000, 16);
    run_matrix(500, 64);

    logger::info("================================================================");
    logger::shutdown();
    return 0;
}
