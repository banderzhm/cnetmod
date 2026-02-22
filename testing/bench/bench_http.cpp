/// cnetmod benchmark — HTTP Server performance
/// Compare with: Boost.Beast, nginx, drogon, oat++, Rust actix-web, Go net/http
///
/// Self-contained: starts HTTP server on localhost, clients send raw HTTP/1.1 requests.

import std;
import cnetmod.coro.spawn;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;

#include "bench_framework.hpp"

using namespace cnetmod;
using namespace cnetmod::http;
using namespace cnetmod::bench;

// =============================================================================
// Minimal HTTP client (raw socket, no parsing overhead)
// =============================================================================

static const std::string HTTP_GET_REQUEST =
    "GET /hello HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";

static auto http_client_keepalive(io_context& ctx, const endpoint& ep,
                                  std::size_t requests) -> task<std::size_t> {
    auto sock_r = socket::create(address_family::ipv4, socket_type::stream);
    if (!sock_r) co_return 0;
    socket sock = std::move(*sock_r);

    auto conn = co_await async_connect(ctx, sock, ep);
    if (!conn) co_return 0;

    std::array<std::byte, 4096> recv_buf{};
    std::size_t completed = 0;

    for (std::size_t i = 0; i < requests; ++i) {
        auto wr = co_await async_write(ctx, sock,
            const_buffer{HTTP_GET_REQUEST.data(), HTTP_GET_REQUEST.size()});
        if (!wr) break;

        // Read response (simple: just read whatever is available)
        auto rd = co_await async_read(ctx, sock,
            mutable_buffer{recv_buf.data(), recv_buf.size()});
        if (!rd || *rd == 0) break;
        ++completed;
    }

    sock.close();
    co_return completed;
}

// =============================================================================
// Benchmark runner
// =============================================================================

static void run_http_bench(std::size_t requests_per_client, std::size_t num_clients,
                           const char* label) {
    auto ctx = make_io_context();

    // Set up HTTP server
    server srv(*ctx);
    router rt;
    rt.get("/hello", [](request_context& rctx) -> task<void> {
        rctx.text(status::ok, "Hello, World!");
        co_return;
    });
    srv.set_router(std::move(rt));

    // Try ports in ephemeral range
    std::uint16_t port = 18080;
    bool listened = false;
    for (int attempt = 0; attempt < 100; ++attempt, ++port) {
        if (srv.listen("127.0.0.1", port)) { listened = true; break; }
    }
    if (!listened) {
        logger::error("  {}: listen failed", label);
        return;
    }

    auto server_ep = endpoint{ipv4_address::loopback(), port};

    // Start server
    spawn(*ctx, srv.run());

    // Measure
    using clock = std::chrono::steady_clock;
    auto start = clock::now();

    std::atomic<std::size_t> total_requests{0};
    std::atomic<std::size_t> done_clients{0};

    auto run_client = [&](std::size_t) -> task<void> {
        auto n = co_await http_client_keepalive(*ctx, server_ep, requests_per_client);
        total_requests.fetch_add(n);
        if (done_clients.fetch_add(1) + 1 >= num_clients) {
            srv.stop();
        }
    };

    for (std::size_t i = 0; i < num_clients; ++i) {
        spawn(*ctx, run_client(i));
    }

    ctx->run();
    auto end = clock::now();

    double elapsed_sec = std::chrono::duration<double>(end - start).count();
    double rps = static_cast<double>(total_requests.load()) / elapsed_sec;

    logger::info("  {:<38}{:>14} req/sec  ({} total)", label, format_number(rps), total_requests.load());
}

// =============================================================================
// Main
// =============================================================================

int main() {
    logger::info("================================================================");
    logger::info("  cnetmod HTTP Server Benchmark");
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
    logger::info("================================================================");

    logger::info("--- Keep-Alive ---");
    run_http_bench(10'000, 1,   "1 conn x 10K req (keep-alive)");
    run_http_bench(5'000,  10,  "10 conn x 5K req (keep-alive)");
    run_http_bench(2'000,  64,  "64 conn x 2K req (keep-alive)");
    run_http_bench(500,    256, "256 conn x 500 req (keep-alive)");

    logger::info("================================================================");
    return 0;
}