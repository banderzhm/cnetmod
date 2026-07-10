/// cnetmod benchmark - gRPC unary throughput over HTTP/2.
///
/// Self-contained: starts a cnetmod gRPC server on localhost and measures
/// cnetmod gRPC client unary calls over persistent HTTP/2 connections.

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;

#include "bench_framework.hpp"

namespace cn = cnetmod;
namespace http = cnetmod::http;
namespace grpc = cnetmod::grpc;

using cnetmod::bench::format_number;

namespace {

constexpr std::string_view kService = "cnetmod.bench.Echo";
constexpr std::string_view kMethod = "Unary";

enum class bench_mode {
    single_loop,
    multicore,
};

auto parse_mode(std::string_view text) -> std::optional<bench_mode> {
    if (text == "single" || text == "single-loop") return bench_mode::single_loop;
    if (text == "multicore" || text == "multi") return bench_mode::multicore;
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

auto make_payload(std::size_t sequence) -> grpc::byte_buffer
{
    grpc::byte_buffer payload;
    payload.resize(16);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>((sequence + i) & 0xff);
    }
    return payload;
}

auto echo_unary(std::span<const std::byte> payload,
                const grpc::call_context&)
    -> cn::task<std::expected<grpc::byte_buffer, grpc::status>>
{
    grpc::byte_buffer out;
    out.assign(payload.begin(), payload.end());
    co_return out;
}

auto run_client(cn::io_context& ctx,
                std::string base_url,
                grpc::client_options opts,
                std::size_t requests,
                std::size_t client_id,
                std::atomic<std::size_t>& completed,
                std::atomic<std::size_t>& failed,
                std::atomic<std::size_t>& done_clients,
                std::size_t client_count,
                http::server& server,
                cn::server_context* server_ctx,
                std::vector<cn::io_context*>& client_contexts) -> cn::task<void>
{
    grpc::client client(ctx, std::move(base_url), std::move(opts));

    for (std::size_t i = 0; i < requests; ++i) {
        auto payload = make_payload(client_id * requests + i);
        const auto expected_size = payload.size();
        auto response = co_await client.unary(grpc::unary_request{
            .service = std::string(kService),
            .method = std::string(kMethod),
            .payload = std::move(payload),
        });
        if (!response || !response->st.ok() || response->payload.size() != expected_size) {
            failed.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        completed.fetch_add(1, std::memory_order_relaxed);
    }

    client.close();

    if (done_clients.fetch_add(1, std::memory_order_acq_rel) + 1 >= client_count) {
        server.stop();
        if (server_ctx) server_ctx->stop();
        for (auto* client_ctx : client_contexts) {
            client_ctx->stop();
        }
    }
}

template <class ServerFactory>
void run_grpc_bench_impl(std::size_t requests_per_client,
                         std::size_t client_count,
                         const char* label,
                         std::string_view mode_name,
                         ServerFactory&& server_factory,
                         cn::server_context* server_ctx,
                         std::vector<cn::io_context*>& client_contexts,
                         std::function<void()> run_server,
                         std::function<void()> stop_server)
{
    grpc::service_router grpc_router;
    grpc_router.add_unary(std::string(kService), std::string(kMethod), echo_unary);

    http::router router;
    router.any("/*path", grpc_router.make_http_handler());

    http::server server = server_factory();
    server.set_router(std::move(router));

    std::uint16_t port = 19100;
    bool listened = false;
    for (int attempt = 0; attempt < 100; ++attempt, ++port) {
        if (server.listen("127.0.0.1", port)) {
            listened = true;
            break;
        }
    }
    if (!listened) {
        logger::error("  {}: listen failed", label);
        return;
    }

    grpc::client_options opts;
    opts.http.version_pref = http::http_version_preference::http2_only;
    opts.http.follow_redirects = false;
    opts.http.enable_cookies = false;
    opts.accept_gzip = false;

    const auto base_url = std::format("http://127.0.0.1:{}", port);
    std::atomic<std::size_t> completed{0};
    std::atomic<std::size_t> failed{0};
    std::atomic<std::size_t> done_clients{0};

    if (server_ctx) {
        cn::spawn(server_ctx->accept_io(), server.run());
    } else {
        cn::spawn(*client_contexts.front(), server.run());
    }

    const auto started = std::chrono::steady_clock::now();
    run_server();
    for (std::size_t i = 0; i < client_count; ++i) {
        auto& client_ctx = *client_contexts[i % client_contexts.size()];
        cn::spawn(client_ctx, run_client(client_ctx,
                                         base_url,
                                         opts,
                                         requests_per_client,
                                         i,
                                         completed,
                                         failed,
                                         done_clients,
                                         client_count,
                                         server,
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
    const auto total = completed.load(std::memory_order_relaxed);
    const auto errors = failed.load(std::memory_order_relaxed);
    const double rps = elapsed > 0.0 ? static_cast<double>(total) / elapsed : 0.0;

    logger::info("  {:<34} {:<10}{:>14} req/sec  ({} ok, {} failed)",
                 label, mode_name, format_number(rps), total, errors);
}

void run_grpc_bench_single(std::size_t requests_per_client,
                           std::size_t client_count,
                           const char* label)
{
    auto ctx = cn::make_io_context();
    std::vector<cn::io_context*> client_contexts{ctx.get()};
    run_grpc_bench_impl(requests_per_client,
                        client_count,
                        label,
                        "single",
                        [&]() { return http::server(*ctx); },
                        nullptr,
                        client_contexts,
                        [] {},
                        [] {});
}

void run_grpc_bench_multicore(std::size_t requests_per_client,
                              std::size_t client_count,
                              const char* label)
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

    run_grpc_bench_impl(requests_per_client,
                        client_count,
                        label,
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

void run_grpc_bench(std::size_t requests_per_client,
                    std::size_t client_count,
                    const char* label,
                    bench_mode mode)
{
    if (mode == bench_mode::single_loop) {
        run_grpc_bench_single(requests_per_client, client_count, label);
    } else {
        run_grpc_bench_multicore(requests_per_client, client_count, label);
    }
}

} // namespace

int main(int argc, char** argv)
{
    cn::net_init net;

    logger::info("================================================================");
    logger::info("  cnetmod gRPC Unary Benchmark");
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
    logger::info("  Transport : HTTP/2 cleartext, persistent connections");
    logger::info("  Handler   : unary echo, 16-byte payload");
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
            std::println("usage: bench_grpc [requests_per_client clients [single|multicore]]");
            return 2;
        }
        auto mode = bench_mode::multicore;
        if (argc == 4) {
            auto parsed = parse_mode(argv[3]);
            if (!parsed) {
                std::println("usage: bench_grpc [requests_per_client clients [single|multicore]]");
                logger::shutdown();
                return 2;
            }
            mode = *parsed;
        }
        run_grpc_bench(requests, clients, "custom unary", mode);
        logger::info("================================================================");
        logger::shutdown();
        return 0;
    }

    run_grpc_bench(10'000, 1, "1 conn x 10K unary", bench_mode::single_loop);
    run_grpc_bench(5'000, 16, "16 conn x 5K unary", bench_mode::multicore);
    run_grpc_bench(2'000, 64, "64 conn x 2K unary", bench_mode::multicore);

    logger::info("================================================================");
    logger::shutdown();
    return 0;
}
