/// cnetmod example — Modbus RTU (Serial) Client/Server Demo
/// 演示 Modbus RTU 串口协议的客户端和服务端功能

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
using namespace cn::modbus;

// ─────────────────────────────────────────────────────────────────────────────
// RTU Server (Slave)
// ─────────────────────────────────────────────────────────────────────────────

auto run_rtu_server(cn::io_context& ctx, std::string_view port_name) -> cn::task<void> {
    std::println("\n── Starting Modbus RTU Server (Slave) ──");

    // 创建数据存储
    memory_data_store store;
    
    // 初始化测试数据
    for (std::uint16_t i = 0; i < 100; ++i) {
        store.write_holding_register(i, 1000 + i);
        store.write_input_register(i, 2000 + i);
        store.write_coil(i, i % 4 == 0);
        store.write_discrete_input(i, i % 3 == 0);
    }

    std::println("Initialized data store with test data");

    // 配置RTU服务器
    rtu_server_config config;
    config.port_name = std::string(port_name);
    config.baudrate = 19200;
    config.data_bits = 8;
    config.stop = stop_bits::one;
    config.par = parity::none;
    config.unit_id = 1;  // Slave address

    std::println("RTU Server configuration:");
    std::println("  Port: {}", config.port_name);
    std::println("  Baudrate: {}", config.baudrate);
    std::println("  Data bits: {}", config.data_bits);
    std::println("  Unit ID: {}", config.unit_id);

    // 创建并启动RTU服务器
    rtu_server server(ctx, store);
    auto start_result = co_await server.start(config);
    
    if (start_result) {
        std::println("Failed to start RTU server: {}", start_result.message());
        co_return;
    }

    std::println("RTU Server started and listening on {}", config.port_name);
    
    // 服务器会一直运行
    while (server.is_running()) {
        co_await cn::async_sleep(ctx, std::chrono::seconds(1));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RTU Client (Master)
// ─────────────────────────────────────────────────────────────────────────────

auto run_rtu_client(cn::io_context& ctx, std::string_view port_name) -> cn::task<void> {
    // 等待服务器启动
    co_await cn::async_sleep(ctx, std::chrono::seconds(1));

    std::println("\n── Starting Modbus RTU Client (Master) ──");

    // 配置RTU客户端
    rtu_config config;
    config.port_name = std::string(port_name);
    config.baudrate = 19200;
    config.data_bits = 8;
    config.stop = stop_bits::one;
    config.par = parity::none;

    std::println("RTU Client configuration:");
    std::println("  Port: {}", config.port_name);
    std::println("  Baudrate: {}", config.baudrate);

    rtu_client client(ctx);
    auto open_result = co_await client.open(config);
    
    if (open_result) {
        std::println("Failed to open RTU client: {}", open_result.message());
        co_return;
    }

    std::println("RTU Client opened on {}", config.port_name);

    request_builder builder;
    builder.set_unit_id(1).set_transport(transport_type::rtu);

    // 1. 读取保持寄存器
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
        } else {
            std::println("   Exception: {}", exception_code_name(parser.get_exception()));
        }
    } else {
        std::println("   Failed: {}", read_resp.error().message());
    }

    // 2. 写入单个寄存器
    std::println("\n2. Writing single register...");
    auto write_req = builder.write_single_register(10, 5555);
    auto write_resp = co_await client.execute(write_req);
    
    if (write_resp && !response_parser(*write_resp).is_exception()) {
        std::println("   Successfully wrote value 5555 to register 10");
    }

    // 3. 读回验证
    std::println("\n3. Verifying write...");
    auto verify_req = builder.read_holding_registers(10, 1);
    auto verify_resp = co_await client.execute(verify_req);
    
    if (verify_resp) {
        response_parser parser(*verify_resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers && !registers->empty()) {
                std::println("   Verification: Register[10] = {}", (*registers)[0]);
            }
        }
    }

    // 4. 批量写入
    std::println("\n4. Writing multiple registers...");
    std::vector<std::uint16_t> values = {111, 222, 333, 444, 555};
    auto batch_write_req = builder.write_multiple_registers(20, values);
    auto batch_write_resp = co_await client.execute(batch_write_req);
    
    if (batch_write_resp && !response_parser(*batch_write_resp).is_exception()) {
        std::println("   Successfully wrote {} registers starting at address 20", values.size());
    }

    // 5. 读取线圈
    std::println("\n5. Reading coils...");
    auto coil_req = builder.read_coils(0, 16);
    auto coil_resp = co_await client.execute(coil_req);
    
    if (coil_resp) {
        response_parser parser(*coil_resp);
        if (!parser.is_exception()) {
            auto coils = parser.parse_bits();
            if (coils) {
                std::println("   Read {} coils:", std::min(coils->size(), std::size_t(16)));
                for (std::size_t i = 0; i < std::min(coils->size(), std::size_t(16)); ++i) {
                    std::println("     Coil[{}] = {}", i, (*coils)[i] ? "ON" : "OFF");
                }
            }
        }
    }

    // 6. 使用重试机制
    std::println("\n6. Testing retry mechanism...");
    auto retry_req = builder.read_input_registers(0, 5);
    auto retry_resp = co_await client.execute_with_retry(retry_req, 3);
    
    if (retry_resp) {
        response_parser parser(*retry_resp);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            if (registers) {
                std::println("   Read {} input registers (with retry):", registers->size());
                for (std::size_t i = 0; i < registers->size(); ++i) {
                    std::println("     Input[{}] = {}", i, (*registers)[i]);
                }
            }
        }
    }

    client.close();
    std::println("\nRTU Client finished");
    
    ctx.stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto main(int argc, char* argv[]) -> int {
    std::println("=== cnetmod: Modbus RTU (Serial) Demo ===\n");
    std::println("This demo shows:");
    std::println("  - Modbus RTU Server (Slave)");
    std::println("  - Modbus RTU Client (Master)");
    std::println("  - Serial communication with CRC16");
    std::println("  - Frame timing and character timeout");

    // 获取串口名称
    std::string port_name;
    if (argc > 1) {
        port_name = argv[1];
    } else {
#ifdef CNETMOD_PLATFORM_WINDOWS
        port_name = "COM3";  // Windows default
#else
        port_name = "/dev/ttyUSB0";  // Linux default
#endif
    }

    std::println("\nUsing serial port: {}", port_name);
    std::println("(You can specify a different port as command line argument)\n");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // 注意：在实际应用中，客户端和服务端通常在不同的设备上
    // 这里为了演示，使用同一个串口（需要硬件回环或虚拟串口对）
    
    // 启动服务器和客户端
    cn::spawn(*ctx, run_rtu_server(*ctx, port_name));
    cn::spawn(*ctx, run_rtu_client(*ctx, port_name));

    ctx->run();

    return 0;
}
