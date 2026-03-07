# cnetmod Examples

Comprehensive examples demonstrating cnetmod framework features.

## Directory Structure

```
examples/
├── core/           # Core functionality (file I/O, serial port, timers, echo servers)
├── concurrency/    # Coroutine primitives (channels, mutex, blocking bridge)
├── http/           # HTTP/1.1 and HTTP/2 servers
├── websocket/      # WebSocket servers
├── database/       # MySQL operations (CRUD, ORM, transactions, MyBatis)
├── redis/          # Redis client and connection pools
├── mqtt/           # MQTT client with pub/sub
├── modbus/         # Modbus protocol (TCP, UDP, RTU)
├── mappers/        # MyBatis XML mapper files
└── test_ssl/       # SSL/TLS certificates for testing
```

## Quick Start

### Build All Examples

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Build Specific Example

```bash
cmake --build . --target <example_name>
```

### Run Example

```bash
./<category>/<example_name>
```

## Categories

###  Core
Basic framework functionality:
- Async file I/O
- Serial port communication
- Timer operations
- TCP echo servers
- SSL/TLS echo servers

[View Core Examples →](core/README.md)

###  Concurrency
Coroutine-based concurrency primitives:
- Async channels (lock-free)
- Async mutex
- Blocking operation bridge
- stdexec integration

[View Concurrency Examples →](concurrency/README.md)

###  HTTP
HTTP server implementations:
- HTTP/1.1 basic server
- HTTP/2 with multiplexing
- High-performance servers
- Multi-core servers
- TechEmpower benchmarks

[View HTTP Examples →](http/README.md)

###  WebSocket
Real-time WebSocket servers:
- Basic WebSocket server
- High-performance server
- Multi-core server

[View WebSocket Examples →](websocket/README.md)

###  Database
MySQL database operations:
- CRUD operations
- ORM (Object-Relational Mapping)
- Transactions
- MyBatis-style XML mappers
- MyBatis Plus features

[View Database Examples →](database/README.md)

###  Redis
Redis client and connection pools:
- Basic client operations
- Connection pool (P0-P6 optimizations)
- Sharded pool (multi-core)

[View Redis Examples →](redis/README.md)

###  MQTT
MQTT pub/sub messaging:
- Connect/disconnect
- Publish/subscribe
- QoS levels (0, 1, 2)
- Retained messages
- Last Will and Testament

[View MQTT Examples →](mqtt/README.md)

###  Modbus
Industrial protocol implementation:
- Modbus TCP client/server
- Modbus UDP client/server
- Modbus RTU client/server (serial)
- Connection pool
- Data stores (mutex-based and channel-based)
