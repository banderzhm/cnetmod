/// cnetmod benchmark - HTTP/1.1 and HTTP/2 over cleartext and TLS.
///
/// This benchmark intentionally uses cnetmod::http::client instead of a raw
/// socket client so the measured path covers DNS/connect, TLS/ALPN, h2c
/// preface, keep-alive reuse, response parsing, and timeout cancellation.

#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.http;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

#include "bench_framework.hpp"

namespace cn = cnetmod;
namespace http = cnetmod::http;

using cnetmod::bench::format_number;

namespace {

struct bench_case {
    const char* name;
    bool tls;
    http::http_version_preference version;
    std::uint16_t port_base;
};

constexpr bench_case cases[] = {
    {"HTTP/1.1 cleartext", false, http::http_version_preference::http1_only, 18080},
    {"HTTP/2 h2c",        false, http::http_version_preference::http2_only, 18180},
    {"HTTPS/1.1",         true,  http::http_version_preference::http1_only, 18280},
    {"HTTPS/2",           true,  http::http_version_preference::http2_only, 18380},
};

enum class bench_mode {
    single_loop,
    multicore,
    both,
};

auto make_cert_path(std::string_view file) -> std::string {
    auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    return (root / "examples" / "test_ssl" / file).string();
}

auto parse_mode(std::string_view text) -> std::optional<bench_mode> {
    if (text == "single" || text == "single-loop") return bench_mode::single_loop;
    if (text == "multicore" || text == "multi") return bench_mode::multicore;
    if (text == "both") return bench_mode::both;
    return std::nullopt;
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

auto make_router() -> http::router {
    http::router rt;
    rt.get("/hello", [](http::request_context& ctx) -> cn::task<void> {
        ctx.text(http::status::ok, "Hello, World!");
        co_return;
    });
    return rt;
}

auto run_http_client(cn::io_context& ctx,
                     const bench_case& cfg,
                     std::string url,
                     std::size_t requests) -> cn::task<std::size_t>
{
    http::client_options opts;
    opts.version_pref = cfg.version;
    opts.follow_redirects = false;
    opts.enable_cookies = false;
    const bool keep_alive = env_u32("CNETMOD_BENCH_HTTP_SHORT_CONNECTIONS", 0) == 0;
    opts.keep_alive = keep_alive;
    opts.verify_peer = false;

    http::client client(ctx, std::move(opts));
    std::size_t completed = 0;

    for (std::size_t i = 0; i < requests; ++i) {
        auto resp = co_await client.get((i == 0 || !keep_alive)
                                            ? std::string_view{url}
                                            : std::string_view{"/hello"});
        if (!resp || resp->status_code() != http::status::ok ||
            resp->body() != "Hello, World!") {
            break;
        }
        ++completed;
    }

    client.close();
    co_return completed;
}

auto run_bench_client(cn::io_context& ctx,
                      bench_case cfg,
                      std::string url,
                      std::size_t requests_per_client,
                      std::size_t client_count,
                      std::atomic<std::size_t>& total,
                      std::atomic<std::size_t>& done,
                      http::server& srv,
                      cn::server_context* server_ctx,
                      std::vector<cn::io_context*>& client_contexts) -> cn::task<void>
{
    auto n = co_await run_http_client(ctx, cfg, std::move(url), requests_per_client);
    total.fetch_add(n, std::memory_order_relaxed);
    if (done.fetch_add(1, std::memory_order_acq_rel) + 1 >= client_count) {
        srv.stop();
        if (server_ctx) server_ctx->stop();
        for (auto* client_ctx : client_contexts) {
            client_ctx->stop();
        }
    }
}

template <class ServerFactory>
void run_http_bench_impl(const bench_case& cfg,
                         std::size_t requests_per_client,
                         std::size_t client_count,
                         std::string_view mode_name,
                         ServerFactory&& server_factory,
                         cn::server_context* server_ctx,
                         std::vector<cn::io_context*>& client_contexts,
                         std::function<void()> run_server,
                         std::function<void()> stop_server)
{
    http::server srv = server_factory();
    srv.set_router(make_router());

#ifdef CNETMOD_HAS_SSL
    std::optional<cn::ssl_context> ssl_ctx;
    if (cfg.tls) {
        auto ssl_ctx_r = cn::ssl_context::server();
        if (!ssl_ctx_r) {
            logger::error("  {:<18}: ssl_context::server failed: {}",
                          cfg.name, ssl_ctx_r.error().message());
            return;
        }

        ssl_ctx.emplace(std::move(*ssl_ctx_r));
        auto cert = make_cert_path("cert.pem");
        auto key = make_cert_path("key.pem");
        if (auto r = ssl_ctx->load_cert_file(cert); !r) {
            logger::error("  {:<18}: load_cert_file failed: {}", cfg.name, r.error().message());
            return;
        }
        if (auto r = ssl_ctx->load_key_file(key); !r) {
            logger::error("  {:<18}: load_key_file failed: {}", cfg.name, r.error().message());
            return;
        }

        if (cfg.version == http::http_version_preference::http2_only) {
            ssl_ctx->configure_alpn_server({"h2"});
        } else {
            ssl_ctx->configure_alpn_server({"http/1.1"});
        }
        srv.set_ssl_context(*ssl_ctx);
    }
#else
    if (cfg.tls) {
        logger::info("  {:<18}{:>14} req/sec  (skipped: SSL disabled)",
                     cfg.name, "-");
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
        logger::error("  {:<18}: listen failed", cfg.name);
        return;
    }

    const auto scheme = cfg.tls ? "https" : "http";
    const auto url = std::format("{}://127.0.0.1:{}/hello", scheme, port);

    std::atomic<std::size_t> total{0};
    std::atomic<std::size_t> done{0};

    if (server_ctx) {
        cn::spawn(server_ctx->accept_io(), srv.run());
    } else {
        cn::spawn(*client_contexts.front(), srv.run());
    }

    const auto started = std::chrono::steady_clock::now();
    run_server();
    for (std::size_t i = 0; i < client_count; ++i) {
        auto& client_ctx = *client_contexts[i % client_contexts.size()];
        cn::spawn(client_ctx, run_bench_client(client_ctx,
                                              cfg,
                                              url,
                                              requests_per_client,
                                              client_count,
                                              total,
                                              done,
                                              srv,
                                              server_ctx,
                                              client_contexts));
    }

    if (client_contexts.size() == 1 && !server_ctx) {
        client_contexts.front()->run();
    } else {
        std::vector<std::jthread> client_threads;
        client_threads.reserve(client_contexts.size());
        for (auto* client_ctx : client_contexts) {
            client_threads.emplace_back([client_ctx] {
                client_ctx->run();
            });
        }
        for (auto& t : client_threads) {
            if (t.joinable()) t.join();
        }
    }
    stop_server();

    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto ok = total.load(std::memory_order_relaxed);
    const auto expected = requests_per_client * client_count;
    const double rps = elapsed > 0.0 ? static_cast<double>(ok) / elapsed : 0.0;

    logger::info("  {:<18} {:<10} {:>4} x {:<7}{:>14} req/sec  ({} ok, {} failed)",
                 cfg.name,
                 mode_name,
                 client_count,
                 requests_per_client,
                 format_number(rps),
                 ok,
                 expected - ok);
}

void run_http_bench_single(const bench_case& cfg,
                           std::size_t requests_per_client,
                           std::size_t client_count)
{
    auto ctx = cn::make_io_context();
    std::vector<cn::io_context*> client_contexts{ctx.get()};
    run_http_bench_impl(cfg,
                        requests_per_client,
                        client_count,
                        "single",
                        [&]() { return http::server(*ctx); },
                        nullptr,
                        client_contexts,
                        [] {},
                        [] {});
}

void run_http_bench_multicore(const bench_case& cfg,
                              std::size_t requests_per_client,
                              std::size_t client_count)
{
    auto fallback_workers = default_worker_count(client_count);
    auto server_workers = env_u32("CNETMOD_BENCH_SERVER_WORKERS", fallback_workers);
    auto client_workers = env_u32("CNETMOD_BENCH_CLIENT_WORKERS", fallback_workers);
    client_workers = std::max(1u, std::min<unsigned>(client_workers, static_cast<unsigned>(client_count)));

    cn::server_context sctx(server_workers, server_workers);
    std::optional<std::jthread> server_thread;

    std::vector<std::unique_ptr<cn::io_context>> client_ios;
    std::vector<cn::io_context*> client_contexts;
    client_ios.reserve(client_workers);
    client_contexts.reserve(client_workers);
    for (unsigned i = 0; i < client_workers; ++i) {
        client_ios.push_back(cn::make_io_context());
        client_contexts.push_back(client_ios.back().get());
    }

    run_http_bench_impl(cfg,
                        requests_per_client,
                        client_count,
                        std::format("mc:{}/{}", server_workers, client_workers),
                        [&]() { return http::server(sctx); },
                        &sctx,
                        client_contexts,
                        [&]() {
                            server_thread.emplace([&sctx] {
                                sctx.run();
                            });
                        },
                        [&]() {
                            sctx.stop();
                            if (server_thread && server_thread->joinable()) {
                                server_thread->join();
                            }
                        });
}

void run_matrix(std::size_t requests_per_client, std::size_t client_count, bench_mode mode) {
    for (auto cfg : cases) {
        if (mode == bench_mode::single_loop || mode == bench_mode::both) {
            run_http_bench_single(cfg, requests_per_client, client_count);
        }
        if (mode == bench_mode::multicore || mode == bench_mode::both) {
            run_http_bench_multicore(cfg, requests_per_client, client_count);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    cn::net_init net;

    logger::info("================================================================");
    logger::info("  cnetmod HTTP Benchmark");
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
    logger::info("  Handler   : GET /hello -> 'Hello, World!' (13 bytes)");
    logger::info("  Keep-alive: {}", env_u32("CNETMOD_BENCH_HTTP_SHORT_CONNECTIONS", 0) == 0 ? "on" : "off");
    logger::info("  Matrix    : HTTP/1.1, HTTP/2, HTTPS/1.1, HTTPS/2");
    logger::info("  Mode      : single-loop or multicore; env CNETMOD_BENCH_*_WORKERS overrides");
    logger::info("================================================================");

    if (argc == 3 || argc == 4) {
        std::size_t requests = 0;
        std::size_t clients = 0;
        auto req_text = std::string_view(argv[1]);
        auto client_text = std::string_view(argv[2]);
        auto [req_ptr, req_ec] = std::from_chars(
            req_text.data(), req_text.data() + req_text.size(), requests);
        auto [client_ptr, client_ec] = std::from_chars(
            client_text.data(), client_text.data() + client_text.size(), clients);
        if (req_ec != std::errc{} || client_ec != std::errc{} ||
            req_ptr != req_text.data() + req_text.size() ||
            client_ptr != client_text.data() + client_text.size() ||
            requests == 0 || clients == 0) {
            std::println("usage: bench_http [requests_per_client clients [single|multicore|both]]");
            logger::shutdown();
            return 2;
        }
        auto mode = bench_mode::multicore;
        if (argc == 4) {
            auto parsed = parse_mode(argv[3]);
            if (!parsed) {
                std::println("usage: bench_http [requests_per_client clients [single|multicore|both]]");
                logger::shutdown();
                return 2;
            }
            mode = *parsed;
        }
        run_matrix(requests, clients, mode);
        logger::info("================================================================");
        logger::shutdown();
        return 0;
    }

    logger::info("--- Small/Latency ---");
    run_matrix(1'000, 1, bench_mode::single_loop);
    logger::info("--- Throughput ---");
    run_matrix(1'000, 16, bench_mode::multicore);
    run_matrix(500, 64, bench_mode::multicore);

    logger::info("================================================================");
    logger::shutdown();
    return 0;
}
