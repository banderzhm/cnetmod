# Modbus

> 工业 Modbus TCP/UDP/RTU 协议全栈实现，支持客户端、服务端、连接池与数据转换。

**import**: `import cnetmod.protocol.modbus;`
**CMake**: `-DCNETMOD_ENABLE_MODBUS=ON`
**源码**: `src/protocol/modbus/`

## 场景导航

- 我要读写保持寄存器 / 线圈 → [看这里](#场景寄存器读写)
- 我要启动 Modbus TCP 服务端 → [看这里](#场景tcp-服务端)
- 我要通过串口 RTU 通信 → [看这里](#场景rtu-串口通信)
- 我要使用连接池管理多连接 → [看这里](#场景连接池)
- 我要在寄存器中存取浮点数/32 位整数 → [看这里](#场景数据转换)
- Modbus TCP vs UDP vs RTU 差异 → [看这里](#场景协议对比)

## API 参考

### Modbus 类型

**签名**: `export enum class function_code : std::uint8_t`

| 值 | 说明 |
|---|------|
| `read_coils` | 0x01 读线圈 |
| `read_discrete_inputs` | 0x02 读离散输入 |
| `read_holding_registers` | 0x03 读保持寄存器 |
| `read_input_registers` | 0x04 读输入寄存器 |
| `write_single_coil` | 0x05 写单个线圈 |
| `write_single_register` | 0x06 写单个寄存器 |
| `write_multiple_coils` | 0x0F 写多个线圈 |
| `write_multiple_registers` | 0x10 写多个寄存器 |

**签名**: `export enum class transport_type { tcp, udp, rtu, ascii };`

**签名**: `export enum class exception_code : std::uint8_t` — `illegal_function`, `illegal_data_address`, `illegal_data_value` 等。

辅助函数：
```cpp
auto function_code_name(function_code fc) -> std::string_view;
auto exception_code_name(exception_code ec) -> std::string_view;
auto calculate_crc16(std::span<const std::uint8_t> data) -> std::uint16_t;
```

### `request_builder` — 请求构建器

**签名**: `export class request_builder`

| 方法 | 签名 | 说明 |
|------|------|------|
| `set_transport` | `auto set_transport(transport_type) -> request_builder&` | 设置传输类型 |
| `set_unit_id` | `auto set_unit_id(std::uint8_t) -> request_builder&` | 设置从站地址 |
| `read_coils` | `auto read_coils(uint16_t start, uint16_t qty) -> modbus_request` | 读线圈 |
| `read_holding_registers` | `auto read_holding_registers(uint16_t start, uint16_t qty) -> modbus_request` | 读保持寄存器 |
| `read_input_registers` | `auto read_input_registers(uint16_t start, uint16_t qty) -> modbus_request` | 读输入寄存器 |
| `write_single_register` | `auto write_single_register(uint16_t addr, uint16_t val) -> modbus_request` | 写单个寄存器 |
| `write_multiple_registers` | `auto write_multiple_registers(uint16_t start, std::span<const uint16_t>) -> modbus_request` | 批量写寄存器 |
| `write_multiple_coils` | `auto write_multiple_coils(uint16_t start, std::span<const bool>) -> modbus_request` | 批量写线圈 |

**示例**:
```cpp
import std;
import cnetmod.protocol.modbus;

using namespace cnetmod::modbus;

request_builder builder;
builder.set_unit_id(1).set_transport(transport_type::tcp);

auto req = builder.read_holding_registers(0, 10);
auto resp = co_await client.execute(req);
if (resp) {
    response_parser parser(*resp);
    if (!parser.is_exception()) {
        auto regs = parser.parse_registers();
        for (std::size_t i = 0; i < regs->size(); ++i) {
            std::println("Register[{}] = {}", i, (*regs)[i]);
        }
    }
}
```

### `response_parser` — 响应解析器

**签名**: `export class response_parser`

| 方法 | 签名 | 说明 |
|------|------|------|
| `is_exception` | `auto is_exception() const -> bool` | 是否异常响应 |
| `get_exception` | `auto get_exception() const -> exception_code` | 获取异常码 |
| `parse_bits` | `auto parse_bits() const -> std::expected<std::vector<bool>, std::error_code>` | 解析线圈/离散位 |
| `parse_registers` | `auto parse_registers() const -> std::expected<std::vector<std::uint16_t>, std::error_code>` | 解析寄存器值 |

### `tcp_client` — Modbus TCP 客户端

**签名**: `export class tcp_client`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit tcp_client(io_context&)` | |
| `connect` | `auto connect(string_view host, uint16_t port) -> task<std::error_code>` | 连接服务端 |
| `execute` | `auto execute(const modbus_request&) -> task<std::expected<modbus_response, std::error_code>>` | 执行请求 |
| `execute_with_timeout` | `auto execute_with_timeout(const modbus_request&, duration) -> task<...>` | 带超时执行 |
| `reconnect` | `auto reconnect() -> task<std::error_code>` | 重连 |
| `close` | `void close()` | 关闭连接 |

### `udp_client` — Modbus UDP 客户端

**签名**: `export class udp_client`

与 `tcp_client` 类似，额外提供 `execute_with_retry(req, int retries)` 用于无连接重试。

### `rtu_client` — Modbus RTU 串口客户端

**签名**: `export class rtu_client`

```cpp
export struct rtu_config {
    std::string port_name;
    std::uint32_t baudrate = 9600;
    std::uint8_t data_bits = 8;
    stop_bits stop = stop_bits::one;
    parity par = parity::none;
    std::chrono::microseconds char_timeout = std::chrono::microseconds(1500);
    std::chrono::microseconds frame_delay = std::chrono::microseconds(3500);
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `open` | `auto open(const rtu_config&) -> task<std::error_code>` | 打开串口 |
| `execute` | `auto execute(const modbus_request&) -> task<std::expected<...>>` | 执行请求 |
| `execute_with_retry` | `auto execute_with_retry(const modbus_request&, int = 3) -> task<...>` | 带重试 |

### 服务端

**签名**: `export class tcp_server` / `export class udp_server`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `tcp_server(io_context& ctx, data_store& store)` | 绑定数据存储 |
| `listen` | `auto listen(string_view host, uint16_t port, ...) -> task<std::error_code>` | 监听端口 |
| `async_run` | `auto async_run() -> task<void>` | 启动接受连接 |
| `stop` | `void stop()` | 停止服务 |

RTU 服务端使用 `rtu_server(io_context&, data_store&)` + `start(const rtu_server_config&)`.

### `data_store` — 数据存储

**签名**: `export class data_store` (抽象接口)

```cpp
virtual auto read_holding_register(uint16_t) -> std::expected<uint16_t, exception_code> = 0;
virtual auto write_holding_register(uint16_t, uint16_t) -> std::expected<void, exception_code> = 0;
virtual auto read_coil(uint16_t) -> std::expected<bool, exception_code> = 0;
virtual auto write_coil(uint16_t, bool) -> std::expected<void, exception_code> = 0;
```

内置实现: `memory_data_store`（内存）和 `channel_data_store`（协程安全通道）。

### `connection_pool` — 连接池

**签名**: `export class connection_pool`

```cpp
export struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 502;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `async_run` | `auto async_run() -> task<void>` | 启动连接池 |
| `async_get_connection` | `auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>` | 异步获取连接 |
| `cancel` | `auto cancel() -> task<void>` | 关闭连接池 |

`pooled_connection` 支持 RAII 自动归还，使用 `conn->execute(req)` 操作。

## Do's & Don'ts

- **Do**: 用 `request_builder` 构建请求，不要手动拼装字节
- **Do**: 始终用 `response_parser` 检查 `is_exception()` 再解析数据
- **Do**: RTU 通信时 `set_transport(transport_type::rtu)`，自动附加 CRC16
- **Don't**: 不要在高并发场景为每个请求创建新 `tcp_client`，使用 `connection_pool`
- **Don't**: UDP 无连接保证可靠，务必使用 `execute_with_retry` 或应用层重试

## 场景：协议对比

| 特性 | TCP | UDP | RTU |
|------|-----|-----|-----|
| 连接 | 长连接 | 无连接 | 串口点对点 |
| 帧头 | MBAP Header | MBAP Header | 地址 + CRC16 |
| 可靠性 | TCP 保证 | 需重试 | 需重试 |
| 典型场景 | 工业以太网 | 广播采集 | 串口设备 |

## 参考示例

- `examples/modbus/modbus_demo.cpp` — TCP 客户端/服务端完整流程
- `examples/modbus/modbus_udp_demo.cpp` — UDP 无连接通信
- `examples/modbus/modbus_rtu_demo.cpp` — RTU 串口通信
- `examples/modbus/modbus_converter_demo.cpp` — 数据转换工具（`RegisterConverter`, `BitOps`, `CRC16`, `Hex`）

## 连接池/连接管理（生产级用法）

### Pool API

`connection_pool` 是 Modbus TCP 客户端的内置连接池，自动管理多个 `tcp_client` 连接的生命周期、健康检查和按需分配。

```cpp
// pool_params — 连接池参数
export struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 502;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
    std::chrono::steady_clock::duration health_check_interval = std::chrono::minutes(5);
};

// connection_pool — 连接池核心
export class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
};

// pooled_connection — RAII 连接守卫，析构时自动归还
export class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> tcp_client&;
    auto operator->() noexcept -> tcp_client*;
};
```

**示例 — 连接池管理多设备**:

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
namespace modbus = cn::modbus;

auto poll_devices(cn::io_context& ctx) -> cn::task<void> {
    // 连接池：初始 4 连接，最大 32 连接，定期健康检查
    modbus::pool_params params;
    params.host = "192.168.1.100";   // PLC 网关地址
    params.port = 502;
    params.initial_size = 4;
    params.max_size = 32;
    params.health_check_interval = std::chrono::minutes(2);
    params.retry_interval = std::chrono::seconds(10);

    modbus::connection_pool pool(ctx, params);
    cn::spawn(ctx, pool.async_run());

    // 并发轮询 10 个从站设备
    for (std::uint8_t unit = 1; unit <= 10; ++unit) {
        cn::spawn(ctx, [&pool, unit]() -> cn::task<void> {
            auto conn_r = co_await pool.async_get_connection();
            if (!conn_r) {
                std::println("获取连接失败: unit={}", unit);
                co_return;
            }

            modbus::request_builder builder;
            builder.set_unit_id(unit).set_transport(modbus::transport_type::tcp);
            auto req = builder.read_holding_registers(0, 20);

            auto resp = co_await conn_r->get().execute(req);
            if (resp) {
                modbus::response_parser parser(*resp);
                if (!parser.is_exception()) {
                    auto regs = parser.parse_registers();
                    std::println("Unit {}: {} registers read", unit, regs->size());
                }
            }
            // pooled_connection 析构自动归还连接
        });
    }

    // 监控池状态
    co_await cn::async_sleep(ctx, std::chrono::seconds(60));
    std::println("池状态: size={}, idle={}, waiters={}",
        pool.size(), pool.idle_count(), pool.waiter_count());

    co_await pool.cancel();
}
```

## 多核/集群部署

### 部署模式

Modbus `tcp_server` / `udp_server` 仅接受单个 `io_context&`，**没有内置 `server_context` 构造函数**。多核部署有两种方案：

1. **多实例方案** — 每个 worker 运行独立的 `tcp_server`（监听不同端口，前置负载均衡器）
2. **手动 round-robin 方案** — 在 `accept_io` 接受 TCP 连接后，分发到 worker 的 `io_context` 处理

### 方案 1：多实例 Modbus TCP 服务端

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
namespace modbus = cn::modbus;

auto main() -> int {
    cn::net_init net;
    cn::server_context sctx(4, 4);

    // 每个 worker 运行独立的 Modbus TCP 服务端 + 独立数据存储
    for (auto* io_ptr : sctx.worker_ios()) {
        cn::spawn(*io_ptr, [&sctx, io_ptr]() -> cn::task<void> {
            static std::atomic<std::uint16_t> port_counter{5020};
            auto port = port_counter.fetch_add(1);

            auto store = std::make_shared<modbus::memory_data_store>();
            auto server = std::make_shared<modbus::tcp_server>(*io_ptr, *store);

            auto ec = co_await server->listen("0.0.0.0", port);
            if (ec) {
                std::println("监听失败 port={}: {}", port, ec.message());
                co_return;
            }
            std::println("Modbus TCP 服务端启动 port={}", port);
            co_await server->async_run();
        });
    }

    sctx.run();
    return 0;
}
```

### 方案 2：Modbus 网关（多核 + 连接池）

生产级 Modbus TCP 网关示例：前端接受大量 SCADA 客户端连接，后端通过连接池访问 PLC 设备。

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
namespace modbus = cn::modbus;

constexpr unsigned WORKER_THREADS = 4;

// 每个 worker 持有独立的 Modbus 连接池
struct worker_gateway {
    cn::io_context& io;
    std::shared_ptr<modbus::memory_data_store> store;
    std::shared_ptr<modbus::tcp_server> server;
};

auto main() -> int {
    cn::net_init net;
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 为每个 worker 创建独立的 Modbus 服务端 + 数据存储
    std::vector<worker_gateway> gateways;
    for (auto* io_ptr : sctx.worker_ios()) {
        auto store = std::make_shared<modbus::memory_data_store>();
        auto server = std::make_shared<modbus::tcp_server>(*io_ptr, *store);
        gateways.push_back({*io_ptr, store, server});
    }

    // 在每个 worker 上启动 Modbus 服务端（不同端口，前置 LB）
    for (std::size_t i = 0; i < gateways.size(); ++i) {
        auto& gw = gateways[i];
        auto port = static_cast<std::uint16_t>(5020 + i);
        cn::spawn(gw.io, [&gw, port]() -> cn::task<void> {
            auto ec = co_await gw.server->listen("0.0.0.0", port);
            if (ec) co_return;
            std::println("Modbus 网关 worker 启动 port={}", port);
            co_await gw.server->async_run();
        });
    }

    // 同时在 accept_io 上运行连接池，用于网关主动轮询 PLC
    modbus::pool_params pool_params;
    pool_params.host = "192.168.1.100";
    pool_params.port = 502;
    pool_params.initial_size = 4;
    pool_params.max_size = 32;
    pool_params.health_check_interval = std::chrono::minutes(3);

    auto upstream_pool = std::make_shared<modbus::connection_pool>(
        sctx.accept_io(), pool_params);
    cn::spawn(sctx.accept_io(), upstream_pool->async_run());

    // 定期从 PLC 同步数据到各 worker 的 data_store
    cn::spawn(sctx.accept_io(), [upstream_pool, &gateways]() -> cn::task<void> {
        while (true) {
            co_await cn::async_sleep(sctx.accept_io(), std::chrono::seconds(5));

            auto conn_r = co_await upstream_pool->async_get_connection();
            if (!conn_r) continue;

            modbus::request_builder builder;
            builder.set_unit_id(1).set_transport(modbus::transport_type::tcp);
            auto req = builder.read_holding_registers(0, 100);

            auto resp = co_await conn_r->get().execute(req);
            if (!resp) continue;

            modbus::response_parser parser(*resp);
            if (parser.is_exception()) continue;
            auto regs = parser.parse_registers();
            if (!regs) continue;

            // 同步到所有 worker 的数据存储
            for (auto& gw : gateways) {
                auto& store_regs = gw.store->get_holding_registers();
                for (std::size_t i = 0; i < regs->size() && i < store_regs.size(); ++i) {
                    store_regs[i] = (*regs)[i];
                }
            }
        }
    });

    sctx.run();
    return 0;
}
```
