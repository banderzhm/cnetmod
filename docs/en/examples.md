# Example Programs

This page provides an overview of all example programs included with cnetmod. Each example demonstrates specific features and best practices.

## Basic Examples

### Echo Server (`echo_server.cpp`)

**What it demonstrates**: Basic TCP server with coroutines

```cpp
// Accept connections and echo back received data
task<void> handle_client(tcp_socket client);
task<void> server(io_context& ctx);
```

**Key concepts**:
- TCP socket operations
- `task<T>` coroutines
- `spawn()` for concurrent connections
- RAII socket management

**Run**:
```bash
./build/examples/echo_server
# Connect with: telnet localhost 8080
```

### Timer Demo (`timer_demo.cpp`)

**What it demonstrates**: Async timers and timeouts

```cpp
// Sleep for specified duration
co_await async_sleep(ctx, 2s);

// Timeout an operation
auto result = co_await with_timeout(ctx, operation(), 5s);
```

**Key concepts**:
- `async_sleep()` and `async_sleep_until()`
- `with_timeout()` for cancellable operations
- Timer-based scheduling

**Run**:
```bash
./build/examples/timer_demo
```

## Synchronization Examples

### Mutex Demo (`mutex_demo.cpp`)

**What it demonstrates**: Coroutine-aware synchronization

```cpp
// Exclusive lock
co_await mtx.lock();
// ... critical section ...
mtx.unlock();

// RAII guard
auto guard = co_await mtx.scoped_lock();
```

**Key concepts**:
- `mutex` for exclusive access
- `shared_mutex` for reader-writer locks
- RAII lock guards
- Deadlock avoidance

**Run**:
```bash
./build/examples/mutex_demo
```

### Channel Demo (`channel_demo.cpp`)

**What it demonstrates**: Go-style channels for communication

```cpp
channel<int> ch(10);  // Buffered channel

// Producer
co_await ch.send(42);

// Consumer
auto value = co_await ch.recv();
```

**Key concepts**:
- `channel<T>` for producer-consumer patterns
- Buffered vs unbuffered channels
- Channel closing and iteration
- Multi-producer, multi-consumer

**Run**:
```bash
./build/examples/channel_demo
```

## HTTP Examples

### HTTP Demo (`http_demo.cpp`)

**What it demonstrates**: Full-featured HTTP server

```cpp
http::server srv(ctx);

// Routes
srv.get("/", handler);
srv.post("/api/users", create_user);
srv.get("/api/users/:id", get_user);

// Middleware
srv.use(cors_middleware());
srv.use(jwt_auth_middleware());
```

**Key concepts**:
- HTTP routing with path parameters
- Middleware pipeline
- JSON request/response
- Query parameters and headers

**Run**:
```bash
./build/examples/http_demo
# Visit: http://localhost:8080
```

### High-Performance HTTP (`hight_http.cpp`)

**What it demonstrates**: Optimized HTTP server

**Key concepts**:
- Zero-copy response handling
- Connection pooling
- Keep-alive optimization
- Minimal allocations

**Run**:
```bash
./build/examples/hight_http
```

### Multi-Core HTTP (`multicore_http.cpp`)

**What it demonstrates**: Multi-threaded HTTP server

```cpp
server_context srv_ctx(4);  // 4 worker threads

http::server srv(srv_ctx.get_io_context());
srv.listen(8080);

srv_ctx.run();  // Runs accept + workers
```

**Key concepts**:
- `server_context` for multi-core
- Accept thread + worker threads
- Load balancing across workers
- Thread-safe operations

**Run**:
```bash
./build/examples/multicore_http
# Benchmark: wrk -t4 -c100 -d10s http://localhost:8080/
```

## WebSocket Examples

### WebSocket Demo (`ws_demo.cpp`)

**What it demonstrates**: WebSocket server

```cpp
ws::server srv(ctx);

srv.on_connect([](ws::connection& conn) {
    std::println("Client connected");
});

srv.on_message([](ws::connection& conn, string_view msg) {
    conn.send(msg);  // Echo back
});
```

**Key concepts**:
- WebSocket upgrade from HTTP
- Frame encoding/decoding
- Ping/pong heartbeat
- Binary and text messages

**Run**:
```bash
./build/examples/ws_demo
# Connect with browser: ws://localhost:8080
```

### Multi-Core WebSocket (`multicore_ws.cpp`)

**What it demonstrates**: Scalable WebSocket server

**Key concepts**:
- Multi-threaded WebSocket handling
- Broadcast to all connections
- Connection management
- Memory-efficient message distribution

**Run**:
```bash
./build/examples/multicore_ws
```

## MQTT Examples

### MQTT Demo (`mqtt_demo.cpp`)

**What it demonstrates**: MQTT broker and client

```cpp
// Broker
mqtt::broker brk(ctx);
brk.listen(1883);

// Client
mqtt::client cli(ctx);
co_await cli.connect({.host = "127.0.0.1", .port = 1883});
co_await cli.subscribe("sensor/#", mqtt::qos::at_least_once);
co_await cli.publish("sensor/temp", "22.5", mqtt::qos::exactly_once);
```

**Key concepts**:
- MQTT v3.1.1 and v5.0 protocols
- QoS levels (0, 1, 2)
- Retained messages and will
- Topic wildcards
- Session persistence

**Run**:
```bash
# Terminal 1: Start broker
./build/examples/mqtt_demo broker

# Terminal 2: Run client
./build/examples/mqtt_demo client
```

## Database Examples

### MySQL CRUD (`mysql_crud.cpp`)

**What it demonstrates**: MySQL async client

```cpp
mysql::client cli(ctx);
co_await cli.connect({
    .host = "127.0.0.1",
    .user = "root",
    .password = "password",
    .database = "test"
});

// Execute query
auto result = co_await cli.query("SELECT * FROM users");

// Prepared statement
auto stmt = co_await cli.prepare("INSERT INTO users (name) VALUES (?)");
co_await stmt.execute({param_value::from_string("Alice")});
```

**Key concepts**:
- Async query execution
- Prepared statements
- Connection pooling
- Transaction management
- Result set iteration

**Run**:
```bash
# Requires MySQL server running
./build/examples/mysql_crud
```

### MySQL ORM (`mysql_orm.cpp`)

**What it demonstrates**: Object-relational mapping

```cpp
struct User {
    int64_t id = 0;
    string name;
    optional<string> email;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE)
)

orm::db_session db(cli);

// CRUD operations
co_await db.insert(user);
auto users = co_await db.find_all<User>();
co_await db.update(user);
co_await db.remove(user);

// Query builder
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` = {}", {param_value::from_string("Alice")})
        .order_by("`id` DESC")
        .limit(10)
);
```

**Key concepts**:
- Model definition with macros
- Auto-migration (`sync_schema`)
- CRUD operations
- Query builder
- UUID and Snowflake ID generation

**Run**:
```bash
./build/examples/mysql_orm
```

### Redis Client (`redis_client.cpp`)

**What it demonstrates**: Redis async operations

```cpp
redis::client cli(ctx);
co_await cli.connect("127.0.0.1", 6379);

// String operations
co_await cli.set("key", "value");
auto value = co_await cli.get("key");

// List operations
co_await cli.lpush("mylist", "item1");
auto items = co_await cli.lrange("mylist", 0, -1);

// Hash operations
co_await cli.hset("user:1", "name", "Alice");
auto name = co_await cli.hget("user:1", "name");
```

**Key concepts**:
- RESP protocol
- Pipeline for batch operations
- Pub/sub messaging
- Connection pooling

**Run**:
```bash
# Requires Redis server running
./build/examples/redis_client
```

## Advanced Examples

### SSL Echo Server (`ssl_echo_server.cpp`)

**What it demonstrates**: TLS/SSL encryption

```cpp
ssl_context ssl_ctx(ssl_context::tlsv13_server);
ssl_ctx.use_certificate_file("server.crt");
ssl_ctx.use_private_key_file("server.key");

ssl_stream stream(std::move(socket), ssl_ctx);
co_await stream.async_handshake(ssl_stream::server);

// Now use encrypted stream
co_await stream.async_send(data);
```

**Key concepts**:
- SSL/TLS setup
- Certificate management
- Async handshake
- Encrypted I/O

**Run**:
```bash
# Generate self-signed certificate first
openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt -days 365 -nodes

./build/examples/ssl_echo_server
# Connect with: openssl s_client -connect localhost:8443
```

### Async File I/O (`async_file.cpp`)

**What it demonstrates**: Non-blocking file operations

```cpp
file f(ctx);
co_await f.open("data.txt", file::read_write | file::create);

// Read
char buffer[1024];
auto n = co_await f.async_read(buffer, sizeof(buffer), 0);

// Write
co_await f.async_write("Hello", 5, 0);
```

**Key concepts**:
- IOCP-backed file I/O (Windows)
- Thread pool offload (Linux/macOS)
- Scatter-gather I/O
- File mapping

**Run**:
```bash
./build/examples/async_file
```

### Serial Port (`serial_port.cpp`)

**What it demonstrates**: Serial communication

```cpp
serial_port port(ctx, "COM3");  // or "/dev/ttyUSB0"
port.set_baud_rate(115200);
port.set_parity(serial_port::parity::none);

co_await port.async_write("AT\r\n");
auto response = co_await port.async_read();
```

**Key concepts**:
- Cross-platform serial I/O
- Baud rate and parity configuration
- Async read/write
- Timeout handling

**Run**:
```bash
./build/examples/serial_port
```

### stdexec Bridge (`stdexec_bridge.cpp`)

**What it demonstrates**: Integration with stdexec

```cpp
// Schedule work on stdexec thread pool
auto result = co_await blocking_invoke(ctx, [] {
    return expensive_computation();
});

// Use sender/receiver
auto sender = schedule(ctx) | then([] { return 42; });
auto value = co_await as_awaitable(sender);
```

**Key concepts**:
- `blocking_invoke()` for CPU-bound work
- Sender/receiver composition
- `async_scope` for structured concurrency
- Thread pool integration

**Run**:
```bash
./build/examples/stdexec_bridge
```

### Blocking Bridge (`blocking_bridge_demo.cpp`)

**What it demonstrates**: Call async code from sync context

```cpp
// Sync function calling async code
int sync_function() {
    io_context ctx;
    return blocking_invoke(ctx, async_operation());
}
```

**Key concepts**:
- Bridge between sync and async worlds
- Temporary event loop
- Exception propagation

**Run**:
```bash
./build/examples/blocking_bridge_demo
```

## Performance Examples

### TechEmpower Benchmark (`tfb_benchmark.cpp`)

**What it demonstrates**: Framework Benchmark compliance

**Endpoints**:
- `/json` - JSON serialization
- `/db` - Single database query
- `/queries?queries=N` - Multiple queries
- `/updates?queries=N` - Database updates
- `/plaintext` - Plaintext response

**Key concepts**:
- Maximum performance optimization
- Connection pooling
- Prepared statements
- Zero-copy responses

**Run**:
```bash
./build/examples/tfb_benchmark
# Benchmark: wrk -t4 -c256 -d30s http://localhost:8080/json
```

## Building and Running Examples

### Build All Examples

```bash
cmake -B build -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

### Build Specific Example

```bash
cmake --build build --target echo_server
```

### Run with Arguments

```bash
# HTTP server on custom port
./build/examples/http_demo --port 9000

# MQTT broker with verbose logging
./build/examples/mqtt_demo broker --verbose
```

## Example Source Code

All example source code is available in the [`examples/`](../examples/) directory:

```
examples/
├── echo_server.cpp
├── timer_demo.cpp
├── mutex_demo.cpp
├── channel_demo.cpp
├── http_demo.cpp
├── hight_http.cpp
├── multicore_http.cpp
├── ws_demo.cpp
├── multicore_ws.cpp
├── mqtt_demo.cpp
├── mysql_crud.cpp
├── mysql_orm.cpp
├── redis_client.cpp
├── ssl_echo_server.cpp
├── async_file.cpp
├── serial_port.cpp
├── stdexec_bridge.cpp
├── blocking_bridge_demo.cpp
└── tfb_benchmark.cpp
```

## Next Steps

- **[Quick Start Guide](getting-started.md)** - Learn the basics
- **[Core Concepts](core/coroutines.md)** - Understand coroutines
- **[Protocol Guides](protocols/http.md)** - Deep dive into protocols
- **[API Reference](api/core.md)** - Detailed API documentation
