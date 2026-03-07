# Modbus 协议支持

cnetmod 提供了完整的 Modbus 协议支持，包括 TCP、UDP 和 RTU（串口）传输方式。

## 特性

- **Modbus TCP 客户端和服务端** - 完整实现，支持连接池
- **Modbus UDP 客户端和服务端** - 无连接的 UDP 传输
- **Modbus RTU 客户端和服务端** - 串口通信，支持 CRC16（计划中）
- **所有标准功能码** - 读写线圈、寄存器等
- **高性能连接池** - 基于 MySQL/Redis 连接池架构
- **异步/协程 API** - 现代 C++23 协程接口
- **异常处理** - 完整的 Modbus 异常码支持

## 支持的功能码

### 位访问
- `0x01` - 读线圈
- `0x02` - 读离散输入
- `0x05` - 写单个线圈
- `0x0F` - 写多个线圈

### 16位寄存器访问
- `0x03` - 读保持寄存器
- `0x04` - 读输入寄存器
- `0x06` - 写单个寄存器
- `0x10` - 写多个寄存器

## 快速开始

### TCP 客户端示例

```cpp
import cnetmod.protocol.modbus;
using namespace cnetmod::modbus;

auto example() -> task<void> {
    io_context ctx;
    tcp_client client(ctx);
    
    // 连接到 Modbus TCP 服务器
    co_await client.connect("192.168.1.100", 502);
    
    // 构建并执行请求
    request_builder builder;
    builder.set_unit_id(1);
    
    auto request = builder.read_holding_registers(0, 10);
    auto response = co_await client.execute(request);
    
    // 解析响应
    if (response) {
        response_parser parser(*response);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            // 使用寄存器数据...
        }
    }
}
```

### TCP 服务端示例

```cpp
auto run_server() -> task<void> {
    io_context ctx;
    
    // 创建数据存储
    memory_data_store store;
    
    // 初始化一些数据
    store.write_holding_register(0, 1234);
    store.write_coil(0, true);
    
    // 创建并启动服务器
    tcp_server server(ctx, store);
    co_await server.listen("0.0.0.0", 502);
    co_await server.async_run();
}
```

### 连接池示例

```cpp
auto use_pool() -> task<void> {
    io_context ctx;
    
    pool_params params;
    params.host = "192.168.1.100";
    params.port = 502;
    params.initial_size = 4;
    params.max_size = 16;
    
    connection_pool pool(ctx, params);
    spawn(ctx, pool.async_run());
    
    // 从连接池获取连接
    auto conn = co_await pool.async_get_connection();
    if (conn) {
        request_builder builder;
        auto req = builder.read_holding_registers(0, 10);
        auto resp = co_await (*conn)->execute(req);
        // 连接自动归还到池
    }
}
```

## 请求构建器 API

```cpp
request_builder builder;
builder.set_unit_id(1)
       .set_transport(transport_type::tcp);

// 读操作
auto req1 = builder.read_coils(address, quantity);
auto req2 = builder.read_discrete_inputs(address, quantity);
auto req3 = builder.read_holding_registers(address, quantity);
auto req4 = builder.read_input_registers(address, quantity);

// 写单个
auto req5 = builder.write_single_coil(address, true);
auto req6 = builder.write_single_register(address, value);

// 写多个
std::vector<bool> coils = {true, false, true};
auto req7 = builder.write_multiple_coils(address, coils);

std::vector<uint16_t> registers = {100, 200, 300};
auto req8 = builder.write_multiple_registers(address, registers);
```

## 响应解析器 API

```cpp
response_parser parser(response);

// 检查异常
if (parser.is_exception()) {
    auto exc = parser.get_exception();
    // 处理异常...
}

// 解析数据
auto bits = parser.parse_bits();           // 用于线圈/离散输入
auto registers = parser.parse_registers(); // 用于保持/输入寄存器
auto [addr, val] = parser.parse_write_response(); // 用于写响应
```

## 自定义数据存储

通过继承 `data_store` 实现自己的数据存储：

```cpp
class my_data_store : public data_store {
public:
    auto read_coil(uint16_t address) 
        -> std::expected<bool, exception_code> override {
        // 你的实现
    }
    
    auto write_coil(uint16_t address, bool value) 
        -> std::expected<void, exception_code> override {
        // 你的实现
    }
    
    // 实现其他方法...
};
```

## 传输类型

- `transport_type::tcp` - Modbus TCP（MBAP 头）
- `transport_type::udp` - Modbus UDP（MBAP 头）
- `transport_type::rtu` - Modbus RTU（串口，CRC16）
- `transport_type::ascii` - Modbus ASCII（串口，LRC）

## 连接池架构

Modbus 连接池使用与 MySQL 和 Redis 连接池相同的高性能架构：

- **P0**: 每连接独立生命周期任务
- **P1**: 需求驱动的动态扩容
- **P2**: std::deque 保证地址稳定性
- **P4**: 无锁快速路径 + 原子操作
- **P6**: 位图索引 O(1) 空闲连接查找

## 错误处理

所有操作返回 `std::expected` 用于错误处理：

```cpp
auto result = co_await client.execute(request);
if (!result) {
    std::error_code ec = result.error();
    // 处理错误...
}
```

Modbus 异常被正确处理：

```cpp
if (parser.is_exception()) {
    switch (parser.get_exception()) {
        case exception_code::illegal_function:
            // 功能不支持
            break;
        case exception_code::illegal_data_address:
            // 无效地址
            break;
        case exception_code::illegal_data_value:
            // 无效值
            break;
        // ... 其他异常
    }
}
```

## 性能建议

1. **使用连接池** - 用于高吞吐量应用
2. **批量操作** - 使用 write_multiple_* 函数
3. **正确的单元 ID** - 设置正确的从站地址
4. **超时配置** - 根据网络调整 pool_timeout
5. **连接池大小** - 根据典型负载匹配 initial_size

## 示例

查看 `examples/modbus_demo.cpp` 获取完整的工作示例，演示：
- TCP 客户端操作
- TCP 服务端实现
- 连接池使用
- 批量读写操作
- 错误处理

## 限制

- Modbus RTU（串口）支持计划中但尚未实现
- 每次请求最大寄存器数：125（Modbus 规范）
- 每次请求最大线圈数：2000（Modbus 规范）

## 参考资料

- [Modbus 协议规范](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [Modbus TCP 实现指南](https://modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf)
