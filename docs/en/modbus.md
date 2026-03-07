# Modbus Protocol Support

cnetmod provides comprehensive support for the Modbus protocol, including TCP, UDP, and RTU (Serial) transports.

## Features

- **Modbus TCP Client & Server** - Full implementation with connection pooling
- **Modbus UDP Client & Server** - Connectionless Modbus over UDP
- **Modbus RTU Client & Server** - Serial communication with CRC16 (planned)
- **All Standard Function Codes** - Read/Write Coils, Registers, etc.
- **High-Performance Connection Pool** - Based on MySQL/Redis pool architecture
- **Async/Await API** - Modern C++23 coroutine-based interface
- **Exception Handling** - Proper Modbus exception code support

## Supported Function Codes

### Bit Access
- `0x01` - Read Coils
- `0x02` - Read Discrete Inputs
- `0x05` - Write Single Coil
- `0x0F` - Write Multiple Coils

### 16-bit Register Access
- `0x03` - Read Holding Registers
- `0x04` - Read Input Registers
- `0x06` - Write Single Register
- `0x10` - Write Multiple Registers

## Quick Start

### TCP Client Example

```cpp
import cnetmod.protocol.modbus;
using namespace cnetmod::modbus;

auto example() -> task<void> {
    io_context ctx;
    tcp_client client(ctx);
    
    // Connect to Modbus TCP server
    co_await client.connect("192.168.1.100", 502);
    
    // Build and execute request
    request_builder builder;
    builder.set_unit_id(1);
    
    auto request = builder.read_holding_registers(0, 10);
    auto response = co_await client.execute(request);
    
    // Parse response
    if (response) {
        response_parser parser(*response);
        if (!parser.is_exception()) {
            auto registers = parser.parse_registers();
            // Use registers...
        }
    }
}
```

### TCP Server Example

```cpp
auto run_server() -> task<void> {
    io_context ctx;
    
    // Create data store
    memory_data_store store;
    
    // Initialize some data
    store.write_holding_register(0, 1234);
    store.write_coil(0, true);
    
    // Create and start server
    tcp_server server(ctx, store);
    co_await server.listen("0.0.0.0", 502);
    co_await server.async_run();
}
```

### Connection Pool Example

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
    
    // Get connection from pool
    auto conn = co_await pool.async_get_connection();
    if (conn) {
        request_builder builder;
        auto req = builder.read_holding_registers(0, 10);
        auto resp = co_await (*conn)->execute(req);
        // Connection automatically returned to pool
    }
}
```

## Request Builder API

```cpp
request_builder builder;
builder.set_unit_id(1)
       .set_transport(transport_type::tcp);

// Read operations
auto req1 = builder.read_coils(address, quantity);
auto req2 = builder.read_discrete_inputs(address, quantity);
auto req3 = builder.read_holding_registers(address, quantity);
auto req4 = builder.read_input_registers(address, quantity);

// Write single
auto req5 = builder.write_single_coil(address, true);
auto req6 = builder.write_single_register(address, value);

// Write multiple
std::vector<bool> coils = {true, false, true};
auto req7 = builder.write_multiple_coils(address, coils);

std::vector<uint16_t> registers = {100, 200, 300};
auto req8 = builder.write_multiple_registers(address, registers);
```

## Response Parser API

```cpp
response_parser parser(response);

// Check for exceptions
if (parser.is_exception()) {
    auto exc = parser.get_exception();
    // Handle exception...
}

// Parse data
auto bits = parser.parse_bits();           // For coils/discrete inputs
auto registers = parser.parse_registers(); // For holding/input registers
auto [addr, val] = parser.parse_write_response(); // For write responses
```

## Custom Data Store

Implement your own data store by inheriting from `data_store`:

```cpp
class my_data_store : public data_store {
public:
    auto read_coil(uint16_t address) 
        -> std::expected<bool, exception_code> override {
        // Your implementation
    }
    
    auto write_coil(uint16_t address, bool value) 
        -> std::expected<void, exception_code> override {
        // Your implementation
    }
    
    // Implement other methods...
};
```

## Transport Types

- `transport_type::tcp` - Modbus TCP (MBAP header)
- `transport_type::udp` - Modbus UDP (MBAP header)
- `transport_type::rtu` - Modbus RTU (Serial with CRC16)
- `transport_type::ascii` - Modbus ASCII (Serial with LRC)

## Connection Pool Architecture

The Modbus connection pool uses the same high-performance architecture as MySQL and Redis pools:

- **P0**: Per-connection autonomous lifecycle tasks
- **P1**: Demand-driven dynamic scaling
- **P2**: std::deque for stable addresses
- **P4**: Lock-free fast path with atomic operations
- **P6**: Bitmap-based O(1) idle connection lookup

## Error Handling

All operations return `std::expected` for error handling:

```cpp
auto result = co_await client.execute(request);
if (!result) {
    std::error_code ec = result.error();
    // Handle error...
}
```

Modbus exceptions are properly handled:

```cpp
if (parser.is_exception()) {
    switch (parser.get_exception()) {
        case exception_code::illegal_function:
            // Function not supported
            break;
        case exception_code::illegal_data_address:
            // Invalid address
            break;
        case exception_code::illegal_data_value:
            // Invalid value
            break;
        // ... other exceptions
    }
}
```

## Performance Tips

1. **Use Connection Pool** - For high-throughput applications
2. **Batch Operations** - Use write_multiple_* functions
3. **Proper Unit ID** - Set correct slave address
4. **Timeout Configuration** - Adjust pool_timeout for your network
5. **Pool Sizing** - Match initial_size to typical load

## Examples

See `examples/modbus_demo.cpp` for a complete working example demonstrating:
- TCP client operations
- TCP server implementation
- Connection pool usage
- Batch read/write operations
- Error handling

## Limitations

- Modbus RTU (Serial) support is planned but not yet implemented
- Maximum register count per request: 125 (Modbus specification)
- Maximum coil count per request: 2000 (Modbus specification)

## References

- [Modbus Protocol Specification](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [Modbus TCP Implementation Guide](https://modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf)
