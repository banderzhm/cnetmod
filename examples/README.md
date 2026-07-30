# cnetmod Examples

Comprehensive examples demonstrating cnetmod framework features.

## Directory Structure

```
examples/
├── core/           # Core functionality (file I/O, serial port, timers, echo servers)
├── concurrency/    # Coroutine primitives (channels, mutex, blocking bridge)
├── http/           # HTTP/1.1 and HTTP/2 servers
├── websocket/      # WebSocket servers
├── database/       # Database protocol examples
│   ├── mysql/      # MySQL client, ORM, transactions, and XML mappers
│   ├── postgresql/ # PostgreSQL production HTTP service
│   └── mongodb/    # MongoDB production topology service
├── redis/          # Redis client and connection pools
├── mqtt/           # MQTT client with pub/sub
├── kafka/          # High-concurrency Kafka producer and consumer group
├── amqp091/        # RabbitMQ publishers and listener container
├── amqp10/         # AMQP 1.0 sender and receiver containers
├── modbus/         # Modbus protocol (TCP, UDP, RTU)
├── integration/    # Standalone host-project integration tests
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
MySQL, PostgreSQL, and MongoDB database services:
- CRUD operations
- ORM (Object-Relational Mapping)
- Transactions
- MyBatis-style XML mappers
- MyBatis Plus features

[PostgreSQL production guide →](../docs/en/protocols/postgresql.md)
[MongoDB production guide →](../docs/en/protocols/mongodb.md)

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

### Integration
Standalone projects that model real downstream usage:
- `integration/thirdparty_collision_project`: verifies that cnetmod can be added to a host project that already owns the same third-party libraries. See `docs/en/advanced/thirdparty-dependency-integration.md` and `docs/zh/advanced/thirdparty-dependency-integration.md`.

### Messaging

Production-oriented Kafka, AMQP 0-9-1, and AMQP 1.0 examples cover TLS and
authentication configuration, reliable delivery, acknowledgements, recovery,
flow control, and manual offset/settlement handling.

[View Messaging Guide](../docs/en/protocols/messaging.md)
