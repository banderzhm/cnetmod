/// cnetmod example — Modbus TCP Client/Server Demo
/// 演示 Modbus TCP 协议的客户端和服务端功能
/// 包括读写线圈、寄存器等操作

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
using namespace cn::modbus;

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1: 基础客户端操作
// ─────────────────────────────────────────────────────────────────────────────

auto demo_basic_client(tcp_client& client) -> cn::task<void> {
    std::println("\n── Basic Client Operations ──");

    request_builder builder;
    builder.set_unit_id(1).set_transport(transport_type::tcp);

    // 读取保持寄存器
    auto read_req = builder.read_holding_registers(0, 10);
    auto read_resp = co_await client.execute(read_req);
    
    if (read_resp) {
        response_parser parser(*read_resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers) {
                std::println("Read {} holding registers:", registers->size());
                for (std::size_t i = 0; i < registers->size(); ++i) {
                    std::println("  Register[{}] = {}", i, (*registers)[i]);
                }
            }
        } else {
            std::println("Exception: {}", 
                        exception_code_name(parser.get_exception()));
        }
    }

    // 写入单个寄存器
    auto write_req = builder.write_single_register(0, 1234);
    auto write_resp = co_await client.execute(write_req);
    
    if (write_resp && !response_parser(*write_resp).is_exception()) {
        std::println("Successfully wrote value 1234 to register 0");
    }

    // 读取线圈
    auto coil_req = builder.read_coils(0, 16);
    auto coil_resp = co_await client.execute(coil_req);
    
    if (coil_resp) {
        response_parser parser(*coil_resp);
        if (!parser.is_exception()) {
            auto coils = parser.parse_bits();
            if (coils) {
                std::println("Read {} coils:", std::min(coils->size(), std::size_t(16)));
                for (std::size_t i = 0; i < std::min(coils->size(), std::size_t(16)); ++i) {
                    std::println("  Coil[{}] = {}", i, (*coils)[i] ? "ON" : "OFF");
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2: 批量写入操作
// ─────────────────────────────────────────────────────────────────────────────

auto demo_batch_write(tcp_client& client) -> cn::task<void> {
    std::println("\n── Batch Write Operations ──");

    request_builder builder;
    builder.set_unit_id(1).set_transport(transport_type::tcp);

    // 写入多个寄存器
    std::vector<std::uint16_t> values = {100, 200, 300, 400, 500};
    auto write_req = builder.write_multiple_registers(10, values);
    auto write_resp = co_await client.execute(write_req);
    
    if (write_resp && !response_parser(*write_resp).is_exception()) {
        std::println("Successfully wrote {} registers starting at address 10", values.size());
    }

    // 读回验证
    auto read_req = builder.read_holding_registers(10, static_cast<std::uint16_t>(values.size()));
    auto read_resp = co_await client.execute(read_req);
    
    if (read_resp) {
        response_parser parser(*read_resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers) {
                std::println("Verification - Read back {} registers:", registers->size());
                for (std::size_t i = 0; i < registers->size(); ++i) {
                    std::println("  Register[{}] = {} (expected {})", 
                                10 + i, (*registers)[i], values[i]);
                }
            }
        }
    }

    // 写入多个线圈
    std::vector<bool> coil_values = {true, false, true, true, false, false, true, false};
    auto coil_write_req = builder.write_multiple_coils(20, coil_values);
    auto coil_write_resp = co_await client.execute(coil_write_req);
    
    if (coil_write_resp && !response_parser(*coil_write_resp).is_exception()) {
        std::println("Successfully wrote {} coils starting at address 20", coil_values.size());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 3: 连接池操作
// ─────────────────────────────────────────────────────────────────────────────

auto demo_connection_pool(cn::io_context& ctx) -> cn::task<void> {
    std::println("\n── Connection Pool Operations ──");

    pool_params params;
    params.host = "127.0.0.1";
    params.port = 5020;
    params.initial_size = 2;
    params.max_size = 8;

    connection_pool pool(ctx, params);
    
    // 启动连接池
    cn::spawn(ctx, pool.async_run());
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));

    std::println("Pool initialized: size={}, idle={}", pool.size(), pool.idle_count());

    // 从池中获取连接
    auto conn_result = co_await pool.async_get_connection();
    if (!conn_result) {
        std::println("Failed to get connection from pool");
        co_return;
    }

    auto conn = std::move(*conn_result);
    std::println("Got connection from pool");

    // 使用连接执行操作
    request_builder builder;
    builder.set_unit_id(1).set_transport(transport_type::tcp);
    
    auto req = builder.read_holding_registers(0, 5);
    auto resp = co_await conn->execute(req);
    
    if (resp) {
        response_parser parser(*resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers) {
                std::println("Read {} registers via pool connection", registers->size());
            }
        }
    }

    // 连接自动归还到池
    conn = pooled_connection{};
    std::println("Connection returned to pool: idle={}", pool.idle_count());

    co_await pool.cancel();
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 4: 服务端
// ─────────────────────────────────────────────────────────────────────────────

auto run_server(cn::io_context& ctx) -> cn::task<void> {
    std::println("\n── Starting Modbus TCP Server ──");

    // 创建数据存储
    memory_data_store store;
    
    // 初始化一些测试数据
    for (std::uint16_t i = 0; i < 100; ++i) {
        store.write_holding_register(i, i * 10);
        store.write_coil(i, i % 2 == 0);
    }

    std::println("Initialized data store with test data");

    // 创建并启动服务器
    tcp_server server(ctx, store);
    auto listen_result = co_await server.listen("0.0.0.0", 5020);
    
    if (listen_result) {
        std::println("Failed to start server: {}", listen_result.message());
        co_return;
    }

    std::println("Server listening on 0.0.0.0:5020");
    
    // 运行服务器（会一直运行直到停止）
    co_await server.async_run();
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 5: 客户端连接到服务端
// ─────────────────────────────────────────────────────────────────────────────

auto run_client(cn::io_context& ctx) -> cn::task<void> {
    // 等待服务器启动
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));

    std::println("\n── Starting Modbus TCP Client ──");

    tcp_client client(ctx);
    auto connect_result = co_await client.connect("127.0.0.1", 5020);
    
    if (connect_result) {
        std::println("Failed to connect: {}", connect_result.message());
        co_return;
    }

    std::println("Connected to server at 127.0.0.1:5020");

    // 运行演示
    co_await demo_basic_client(client);
    co_await demo_batch_write(client);

    client.close();
    std::println("\nClient disconnected");
    
    ctx.stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto main() -> int {
    std::println("=== cnetmod: Modbus TCP Demo ===\n");
    std::println("This demo shows:");
    std::println("  - Modbus TCP Server");
    std::println("  - Modbus TCP Client");
    std::println("  - Reading/Writing Coils and Registers");
    std::println("  - Connection Pool");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // 启动服务器和客户端
    cn::spawn(*ctx, run_server(*ctx));
    cn::spawn(*ctx, run_client(*ctx));

    ctx->run();

    return 0;
}
