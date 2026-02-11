/// cnetmod example — Serial Port Communication
/// 演示异步串口读写：打开 COM 口，发送数据，读取响应

#include <cnetmod/config.hpp>

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.serial_port;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;

using namespace cnetmod;

// =============================================================================
// 串口回环测试 (loopback)
// 将 TX/RX 短接，发送的数据会被自己接收
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

    // 读取回环数据
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
// AT 命令交互 (适用于调制解调器 / GSM 模块)
// =============================================================================

auto run_at_command(io_context& ctx, serial_port& port,
                    std::string_view command) -> task<void>
{
    std::println("  >> {}", command);

    // 发送 AT 命令 (追加 \r\n)
    std::string cmd = std::string(command) + "\r\n";
    auto wr = co_await async_serial_write(ctx, port,
        const_buffer{cmd.data(), cmd.size()});

    if (!wr) {
        std::println("  write error: {}", wr.error().message());
        co_return;
    }

    // 读取响应
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
// 连续读取 (监听串口数据)
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
// 主协程
// =============================================================================

auto run_serial_demo(io_context& ctx) -> task<void> {
    // 修改为你的串口名称
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

    // --- 测试 1: 回环测试 ---
    std::println("\n  === Test 1: Loopback ===");
    co_await run_loopback_test(ctx, port);

    // --- 测试 2: AT 命令 ---
    std::println("\n  === Test 2: AT Commands ===");
    co_await run_at_command(ctx, port, "AT");
    co_await run_at_command(ctx, port, "AT+GMR");

    // --- 测试 3: 连续读取 ---
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
