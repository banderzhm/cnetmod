# Modbus Examples

Complete Modbus protocol implementation (TCP, UDP, RTU).

## Examples

### modbus_demo.cpp
Comprehensive Modbus demonstration:
- **TCP Client**: Read/write operations over TCP
- **TCP Server**: Handle Modbus TCP requests
- **UDP Client**: Connectionless Modbus communication
- **UDP Server**: UDP request handling
- **RTU Client**: Serial port communication (RS-232/RS-485)
- **RTU Server**: Serial port slave device
- **Connection Pool**: High-performance connection pooling
- **Data Store**: Memory-based and channel-based storage

## Supported Function Codes

| Code | Function | Description |
|------|----------|-------------|
| 0x01 | Read Coils | Read 1-2000 coils (0x) |
| 0x02 | Read Discrete Inputs | Read 1-2000 discrete inputs (1x) |
| 0x03 | Read Holding Registers | Read 1-125 holding registers (4x) |
| 0x04 | Read Input Registers | Read 1-125 input registers (3x) |
| 0x05 | Write Single Coil | Write single coil |
| 0x06 | Write Single Register | Write single holding register |
| 0x0F | Write Multiple Coils | Write multiple coils |
| 0x10 | Write Multiple Registers | Write multiple holding registers |

## Protocol Variants

### Modbus TCP
- Standard Modbus over TCP/IP
- Port 502 (default)
- MBAP header + PDU
- Connection-oriented

### Modbus UDP
- Modbus over UDP/IP
- Port 502 (default)
- MBAP header + PDU
- Connectionless, with retry

### Modbus RTU
- Modbus over serial (RS-232/RS-485)
- CRC-16 error checking
- Frame timing (3.5 char times)
- Master-slave architecture

## Architecture

### Client Operations

```cpp
// TCP Client
modbus::tcp_client client(ctx);
co_await client.connect("192.168.1.100", 502);

auto req = modbus::request_builder()
    .unit_id(1)
    .read_holding_registers(0, 10)
    .build();

auto resp = co_await client.execute(req);

// RTU Client
modbus::rtu_config config;
config.port_name = "COM3";  // or "/dev/ttyUSB0"
config.baudrate = 9600;

modbus::rtu_client rtu_client(ctx);
co_await rtu_client.open(config);
auto resp = co_await rtu_client.execute(req);
```

### Server Implementation

```cpp
// TCP Server
modbus::memory_data_store store;
modbus::tcp_server server(ctx, store);
co_await server.start("0.0.0.0", 502);

// RTU Server
modbus::rtu_server_config config;
config.port_name = "COM3";
config.unit_id = 1;  // Slave address

modbus::rtu_server rtu_server(ctx, store);
co_await rtu_server.start(config);
```

### Data Store Options

#### memory_data_store (Mutex-based)
```cpp
modbus::memory_data_store store;
// Simple, good for low-medium concurrency
```

#### channel_data_store (Lock-free)
```cpp
modbus::channel_data_store store(10000, 10000, 10000, 10000, 128);
store.start_worker();
spawn(ctx, store.worker());

// High-performance, lock-free channel operations
// Better for high-concurrency scenarios
auto result = co_await store.read_holding_register_async(100);
```

### Connection Pool

```cpp
modbus::pool_config config;
config.host = "192.168.1.100";
config.port = 502;
config.initial_size = 5;
config.max_size = 20;

modbus::connection_pool pool(ctx, config);
co_await pool.start();

// Acquire connection
auto conn = co_await pool.acquire();
auto resp = co_await conn->execute(req);
// Auto-release on destruction
```

## Building

```bash
cd build
cmake --build . --target modbus_demo
```

## Running

```bash
# Run demo (includes client and server examples)
./modbus/modbus_demo

# For RTU examples, ensure serial port is available:
# Windows: COM1, COM2, COM3, etc.
# Linux: /dev/ttyUSB0, /dev/ttyS0, /dev/ttyAMA0, etc.
```

## RTU Configuration

### Timing Parameters

```cpp
modbus::rtu_config config;
config.baudrate = 9600;

// Calculate timing based on baudrate
// Character time = 11 bits / baudrate
// At 9600 baud: 11 / 9600 ≈ 1.146 ms

config.char_timeout = std::chrono::microseconds(1500);   // 1.5 char times
config.frame_delay = std::chrono::microseconds(3500);    // 3.5 char times
```

### Serial Port Settings

```cpp
config.data_bits = 8;
config.stop = stop_bits::one;
config.par = parity::none;  // or even, odd
```

## Testing Tools

### Modbus Poll (Windows)
- GUI tool for testing Modbus devices
- Supports TCP, RTU, ASCII

### pyModbusTCP (Python)
```python
from pyModbusTCP.client import ModbusClient

client = ModbusClient(host="localhost", port=502)
regs = client.read_holding_registers(0, 10)
```

### modpoll (Command-line)
```bash
# Read holding registers
modpoll -m tcp -a 1 -r 0 -c 10 localhost

# Write single register
modpoll -m tcp -a 1 -r 100 -1 1234 localhost
```

## Performance Tips

1. **Use connection pool** for TCP clients
2. **Use channel_data_store** for high-concurrency servers
3. **Batch operations** with write_multiple_* functions
4. **Configure RTU timing** based on baudrate
5. **Set appropriate timeouts** for network conditions

## Use Cases

- Industrial automation (PLC communication)
- SCADA systems
- Building management systems (BMS)
- Energy monitoring
- Process control
- Remote terminal units (RTU)
