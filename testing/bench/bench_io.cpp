/// cnetmod benchmark — TCP Echo I/O throughput
/// Compare with: Boost.Asio echo, libuv echo, muduo echo, io_uring echo
///
/// Self-contained: starts echo server, connects clients, measures throughput.

import std;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;

#include "bench_framework.hpp"

using namespace cnetmod;
using namespace cnetmod::bench;

// =============================================================================
// Echo server coroutine
// =============================================================================

static auto echo_session(io_context& ctx, socket client) -> task<void> {
    std::array<std::byte, 65536> buf{};
    while (true) {
        auto rd = co_await async_read(ctx, client, mutable_buffer{buf.data(), buf.size()});
        if (!rd || *rd == 0) break;
        auto wr = co_await async_write(ctx, client, const_buffer{buf.data(), *rd});
        if (!wr) break;
    }
    client.close();
}

static auto echo_accept_loop(io_context& ctx, socket& listener,
                             std::atomic<bool>& running) -> task<void> {
    while (running.load(std::memory_order_relaxed)) {
        auto r = co_await async_accept(ctx, listener);
        if (!r) {
            if (!running.load()) break;
            continue;
        }
        // Fire-and-forget session
        spawn(ctx, echo_session(ctx, std::move(*r)));
    }
}

// =============================================================================
// Client: send/recv N roundtrips, measure throughput
// =============================================================================

struct echo_result {
    std::size_t roundtrips = 0;
    std::size_t bytes = 0;
};

static auto echo_client(io_context& ctx, const endpoint& server_ep,
                        std::size_t msg_size, std::size_t count) -> task<echo_result> {
    auto sock_r = socket::create(address_family::ipv4, socket_type::stream);
    if (!sock_r) co_return echo_result{};
    socket sock = std::move(*sock_r);

    auto conn = co_await async_connect(ctx, sock, server_ep);
    if (!conn) co_return echo_result{};

    std::vector<std::byte> send_buf(msg_size, std::byte{0x42});
    std::vector<std::byte> recv_buf(msg_size);

    echo_result result;
    for (std::size_t i = 0; i < count; ++i) {
        auto wr = co_await async_write(ctx, sock, const_buffer{send_buf.data(), send_buf.size()});
        if (!wr) break;

        // Read exactly msg_size bytes
        std::size_t total_read = 0;
        while (total_read < msg_size) {
            auto rd = co_await async_read(ctx, sock,
                mutable_buffer{recv_buf.data() + total_read, msg_size - total_read});
            if (!rd || *rd == 0) break;
            total_read += *rd;
        }
        if (total_read < msg_size) break;
        ++result.roundtrips;
        result.bytes += msg_size * 2;  // sent + received
    }

    sock.close();
    co_return result;
}

// =============================================================================
// Benchmark runner: start server, run clients, collect results
// =============================================================================

static void run_echo_bench(std::size_t msg_size, std::size_t roundtrips,
                           std::size_t num_clients, const char* label) {
    auto ctx = make_io_context();
    std::atomic<bool> running{true};

    // Set up listener
    tcp::acceptor acc(*ctx);
    auto ep = endpoint{ipv4_address::loopback(), 0};  // OS-assigned port
    auto listen_r = acc.open(ep, {.reuse_address = true});
    if (!listen_r) {
        logger::error("  {}: listen failed", label);
        return;
    }

    // Get actual port
    auto local_ep_r = acc.native_socket().local_endpoint();
    if (!local_ep_r) return;
    auto server_ep = *local_ep_r;

    // Start accept loop
    spawn(*ctx, echo_accept_loop(*ctx, acc.native_socket(), running));

    // Run clients and measure
    using clock = std::chrono::steady_clock;
    auto start = clock::now();

    // Create client tasks
    std::vector<echo_result> results(num_clients);
    std::atomic<std::size_t> done_count{0};

    auto run_client = [&](std::size_t idx) -> task<void> {
        results[idx] = co_await echo_client(*ctx, server_ep, msg_size, roundtrips);
        if (done_count.fetch_add(1) + 1 >= num_clients) {
            running.store(false);
            acc.close();
            ctx->stop();
        }
    };

    for (std::size_t i = 0; i < num_clients; ++i) {
        spawn(*ctx, run_client(i));
    }

    ctx->run();
    auto end = clock::now();

    // Aggregate results
    std::size_t total_rt = 0, total_bytes = 0;
    for (auto& r : results) {
        total_rt += r.roundtrips;
        total_bytes += r.bytes;
    }

    double elapsed_sec = std::chrono::duration<double>(end - start).count();
    double msgs_per_sec = static_cast<double>(total_rt) / elapsed_sec;
    double mbps = static_cast<double>(total_bytes) * 8.0 / elapsed_sec / 1e6;

    logger::info("  {:<38}{:>12} msg/sec  {:>10.1f} Mbps", label, format_number(msgs_per_sec), mbps);
}

// =============================================================================
// Main
// =============================================================================

int main() {
    logger::info("================================================================");
    logger::info("  cnetmod TCP Echo I/O Benchmark");
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
    logger::info("================================================================");

    logger::info("--- Single Connection ---");
    run_echo_bench(1024,   10'000, 1, "1KB msg x 10K roundtrips");
    run_echo_bench(4096,   10'000, 1, "4KB msg x 10K roundtrips");
    run_echo_bench(65536,   5'000, 1, "64KB msg x 5K roundtrips");

    logger::info("--- Multi Connection ---");
    run_echo_bench(1024,   5'000, 10,  "10 conn x 1KB x 5K rt");
    run_echo_bench(1024,   2'000, 64,  "64 conn x 1KB x 2K rt");
    run_echo_bench(1024,   1'000, 256, "256 conn x 1KB x 1K rt");

    logger::info("================================================================");
    return 0;
}
