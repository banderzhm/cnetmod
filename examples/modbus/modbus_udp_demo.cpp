/// cnetmod example — Modbus UDP Client/Server Demo
/// 演示 Modbus UDP 协议的客户端和服务端功能

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
using namespace cn::modbus;

// ─────────────────────────────────────────────────────────────────────────────
// UDP Server
// ─────────────────────────────────────────────────────────────────────────────

auto run_udp_server(cn::io_context& ctx) -> cn::task<void> {
    std::println("\n── Starting Modbus UDP Server ──");

    // 创建数据存储
    memory_data_store store;
    
    // 初始化测试数据
    for (std::uint16_t i = 0; i < 50; ++i) {
        store.write_holding_register(i, i * 100);
        store.write_input_register(i, i * 50);
        store.write_coil(i, i % 3 == 0);
        store.write_discrete_input(i, i % 2 == 0);
    }

    std::println("Initialized data store with test data");

    // 创建并启动UDP服务器
    udp_server server(ctx, store);
    auto listen_result = co_await server.listen("0.0.0.0", 5021);
    
    if (listen_result) {
        std::println("Failed to start UDP server: {}", listen_result.message());
        co_return;
    }

    std::println("UDP Server listening on 0.0.0.0:5021");
    std::println("Request count: {}", server.get_request_count());
    
    // 运行服务器
    co_await server.async_run();
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP Client
// ─────────────────────────────────────────────────────────────────────────────

auto run_udp_client(cn::io_context& ctx) -> cn::task<void> {
    // 等待服务器启动
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));

    std::println("\n── Starting Modbus UDP Client ──");

    udp_client client(ctx);
    auto connect_result = co_await client.connect("127.0.0.1", 5021);
    
    if (connect_result) {
        std::println("Failed to setup UDP client: {}", connect_result.message());
        co_return;
    }

    std::println("UDP Client ready to communicate with 127.0.0.1:5021");

    request_builder builder;
    builder.set_unit_id(1).set_transport(transport_type::udp);

    // 读取保持寄存器
    std::println("\n1. Reading holding registers...");
    auto read_req = builder.read_holding_registers(0, 10);
    auto read_resp = co_await client.execute(read_req);
    
    if (read_resp) {
        response_parser parser(*read_resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers) {
                std::println("   Read {} holding registers:", registers->size());
                for (std::size_t i = 0; i < std::min(registers->size(), std::size_t(5)); ++i) {
                    std::println("     Register[{}] = {}", i, (*registers)[i]);
                }
            }
        }
    }

    // 读取输入寄存器
    std::println("\n2. Reading input registers...");
    auto input_req = builder.read_input_registers(0, 8);
    auto input_resp = co_await client.execute(input_req);
    
    if (input_resp) {
        response_parser parser(*input_resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers) {
                std::println("   Read {} input registers:", registers->size());
                for (std::size_t i = 0; i < registers->size(); ++i) {
                    std::println("     Input[{}] = {}", i, (*registers)[i]);
                }
            }
        }
    }

    // 写入单个寄存器
    std::println("\n3. Writing single register...");
    auto write_req = builder.write_single_register(5, 9999);
    auto write_resp = co_await client.execute(write_req);
    
    if (write_resp && !response_parser(*write_resp).is_exception()) {
        std::println("   Successfully wrote value 9999 to register 5");
    }

    // 读回验证
    auto verify_req = builder.read_holding_registers(5, 1);
    auto verify_resp = co_await client.execute(verify_req);
    
    if (verify_resp) {
        response_parser parser(*verify_resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers && !registers->empty()) {
                std::println("   Verification: Register[5] = {}", (*registers)[0]);
            }
        }
    }

    // 使用重试机制
    std::println("\n4. Testing retry mechanism...");
    auto retry_req = builder.read_coils(0, 16);
    auto retry_resp = co_await client.execute_with_retry(retry_req, 3);
    
    if (retry_resp) {
        response_parser parser(*retry_resp);
        if (!parser.is_exception()) {
            auto coils = parser.parse_bits();
            if (coils) {
                std::println("   Read {} coils (with retry):", std::min(coils->size(), std::size_t(16)));
                for (std::size_t i = 0; i < std::min(coils->size(), std::size_t(16)); ++i) {
                    std::println("     Coil[{}] = {}", i, (*coils)[i] ? "ON" : "OFF");
                }
            }
        }
    }

    client.close();
    std::println("\nUDP Client finished");
    
    ctx.stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto main() -> int {
    std::println("=== cnetmod: Modbus UDP Demo ===\n");
    std::println("This demo shows:");
    std::println("  - Modbus UDP Server (connectionless)");
    std::println("  - Modbus UDP Client");
    std::println("  - Reading/Writing with UDP transport");
    std::println("  - Retry mechanism for unreliable networks");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // 启动服务器和客户端
    cn::spawn(*ctx, run_udp_server(*ctx));
    cn::spawn(*ctx, run_udp_client(*ctx));

    ctx->run();

    return 0;
}
