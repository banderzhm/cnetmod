/// cnetmod example — Serial Port Communication
/// Demonstratesasyncserial port: COM , , readresponse

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

using namespace cnetmod;

// =============================================================================
// Serial portloopbackTest (loopback)
// Implementation note: TX/RX.
// =============================================================================

auto run_loopback_test(io_context& ctx, serial_port& port) -> task<void> {
    std::println("  [Loopback] Sending test data...");

    std::string tx_data = "Hello, Serial Port! cnetmod C++23\r\n";
    auto wr = co_await async_serial_write(ctx, port,
        const_buffer{tx_data.data(), tx_data.size()});

    if (!wr) {
        std::println("  write error: {}", wr.error().message());
        co_return;
    }
    std::println("  sent {} bytes", *wr);

    // Readloopback
    std::array<char, 256> rx_buf{};
    auto rr = co_await async_serial_read(ctx, port,
        mutable_buffer{rx_buf.data(), rx_buf.size()});

    if (!rr) {
        std::println("  read error: {}", rr.error().message());
        co_return;
    }

    if (*rr > 0) {
        std::string_view received(rx_buf.data(), *rr);
        std::println("  received {} bytes: {}", *rr, received);
    } else {
        std::println("  read timeout (0 bytes) — no loopback detected");
    }
}

// =============================================================================
// AT ( / GSM )
// =============================================================================

auto run_at_command(io_context& ctx, serial_port& port,
                    std::string_view command) -> task<void>
{
    std::println("  >> {}", command);

    // AT ( \r\n)
    std::string cmd = std::string(command) + "\r\n";
    auto wr = co_await async_serial_write(ctx, port,
        const_buffer{cmd.data(), cmd.size()});

    if (!wr) {
        std::println("  write error: {}", wr.error().message());
        co_return;
    }

    // Readresponse
    std::array<char, 1024> rx_buf{};
    auto rr = co_await async_serial_read(ctx, port,
        mutable_buffer{rx_buf.data(), rx_buf.size()});

    if (!rr) {
        std::println("  read error: {}", rr.error().message());
        co_return;
    }

    if (*rr > 0) {
        std::string_view response(rx_buf.data(), *rr);
        std::println("  << {}", response);
    } else {
        std::println("  << (no response)");
    }
}

// =============================================================================
// Read (serial port)
// =============================================================================

auto run_continuous_read(io_context& ctx, serial_port& port,
                         int max_reads) -> task<void>
{
    std::println("  [Monitor] Reading up to {} times...", max_reads);

    std::array<char, 512> rx_buf{};

    for (int i = 0; i < max_reads; ++i) {
        auto rr = co_await async_serial_read(ctx, port,
            mutable_buffer{rx_buf.data(), rx_buf.size()});

        if (!rr) {
            std::println("  read error: {}", rr.error().message());
            break;
        }

        if (*rr > 0) {
            std::string_view data(rx_buf.data(), *rr);
            std::println("  [{}] {} bytes: {}", i + 1, *rr, data);
        } else {
            std::println("  [{}] timeout", i + 1);
        }
    }
}

// =============================================================================
// Main coroutine
// =============================================================================

auto run_serial_demo(io_context& ctx) -> task<void> {
    // Serial port
    constexpr auto port_name = "COM3";

    serial_config cfg;
    cfg.baud_rate       = 115200;
    cfg.data_bits       = 8;
    cfg.stop            = stop_bits::one;
    cfg.par             = parity::none;
    cfg.flow            = flow_control::none;
    cfg.read_timeout_ms = 2000;

    std::println("  Opening {}...", port_name);
    auto port_r = serial_port::open(port_name, cfg);
    if (!port_r) {
        std::println("  Failed to open {}: {}", port_name, port_r.error().message());
        std::println("  (Make sure the port exists and is not in use)");
        co_return;
    }

    auto& port = *port_r;
    std::println("  {} opened (baud={}, {}{}{}, flow={})",
        port_name,
        cfg.baud_rate,
        cfg.data_bits,
        cfg.par == parity::none ? 'N' :
            cfg.par == parity::odd ? 'O' :
            cfg.par == parity::even ? 'E' : '?',
        cfg.stop == stop_bits::one ? '1' :
            cfg.stop == stop_bits::two ? '2' : '?',
        cfg.flow == flow_control::none ? "none" :
            cfg.flow == flow_control::hardware ? "hw" : "sw"
    );

    // Test 1: loopbackTest
    std::println("\n  === Test 1: Loopback ===");
    co_await run_loopback_test(ctx, port);

    // Test 2: AT
    std::println("\n  === Test 2: AT Commands ===");
    co_await run_at_command(ctx, port, "AT");
    co_await run_at_command(ctx, port, "AT+GMR");

    // Test 3: read
    std::println("\n  === Test 3: Continuous Read ===");
    co_await run_continuous_read(ctx, port, 3);

    port.close();
    std::println("\n  Port closed.");
}

auto run_and_stop(io_context& ctx) -> task<void> {
    co_await run_serial_demo(ctx);
    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Serial Port Communication ===");

    auto ctx = make_io_context();
    spawn(*ctx, run_and_stop(*ctx));
    ctx->run();

    std::println("Done.");
    return 0;
}
