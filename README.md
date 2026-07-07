# cnetmod

Cross-platform asynchronous network library using C++23 modules and native coroutines.

[![Linux Build](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml)
[![macOS Build](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml)
[![Windows Build](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml)

English | [简体中文](README_zh.md)

## Platform Support

| Platform | I/O Engine | Compiler | Status |
|----------|-----------|----------|--------|
| Windows | IOCP | Latest Visual Studio 2026 (MSVC) | ✅ |
| Linux | io_uring + epoll | clang-21 + libc++ | ✅ |
| macOS | kqueue | clang-21 + libc++ | ✅ |

## Features

### Core Runtime
- **Coroutine engine**: `task<T>`, `spawn()` fire-and-forget, symmetric transfer for tail-call optimization
- **I/O context**: Platform-native event loop (IOCP / io_uring / epoll / kqueue) with thread-safe `post()`
- **Multi-core**: `server_context` with dedicated accept thread + N worker `io_context` threads + stdexec thread pool
- **stdexec integration**: `schedule()` sender, `async_scope`, `blocking_invoke()` for offloading blocking calls

### Networking
- **TCP**: Async accept / connect / read / write with RAII socket wrappers
- **UDP**: Async sendto / recvfrom
- **TLS/SSL**: OpenSSL-backed `ssl_context` / `ssl_stream` with async handshake, SNI, client certificate support
- **Async DNS**: `async_resolve()` — non-blocking DNS via stdexec thread pool + `getaddrinfo`
- **Serial port**: Cross-platform async serial I/O

### Protocols
- **HTTP/1.1 & HTTP/2**: Full server with router, middleware pipeline, chunked transfer, multipart upload; HTTP/2 via TLS + ALPN negotiation with multiplexed streams
- **WebSocket**: Server-side upgrade from HTTP, frame codec, ping/pong, per-message deflate
- **SOCKS5**: Proxy protocol client and server — CONNECT, BIND, UDP ASSOCIATE commands; authentication methods (no auth, username/password); IPv4, IPv6, and domain name support
- **MQTT v3.1.1 / v5.0**: Full broker + async client — QoS 0/1/2, retained messages, will, session resume, shared subscriptions, topic alias, auto-reconnect; sync client wrapper
- **MySQL**: Async client with prepared statements, connection pool, pipeline, transaction management, ORM (CRUD / migration / query builder / MyBatis-Plus style XML mappers / BaseMapper / pagination / soft delete / optimistic lock / multi-tenant / cache)
- **Redis**: Async client with RESP protocol, connection pool
- **Raft**: Replicated state machine toolkit with leader election, log replication, ReadIndex, leader lease / check-quorum, joint consensus membership changes, snapshot install / compaction, LevelDB-backed persistence, TCP transport, TLS / mTLS authentication, transport metrics, chaos / restart tests, and distributed storage examples
- **Modbus**: Complete protocol implementation — TCP/UDP/RTU (serial) client and server, all standard function codes, connection pool, CRC-16 validation, frame timing control, data stores (mutex-based and lock-free channel-based)
- **CoAP**: CoAP for constrained devices — RFC 7252 message/option codec, confirmable request retransmission, Observe subscriptions/notifications, token/message-id matching, resource router, Block1/Block2 helpers, CoAPS/DTLS context support
- **OpenAI**: Async API client (chat completions, etc.)

### Raft Performance
Release benchmark on Intel Core i9-14900K:

| Scenario | Throughput |
|----------|------------|
| Single-node append + commit | ~5.17M - 5.50M ops/s |
| 5-node majority replication | ~0.80M - 0.83M ops/s |

Use `testing/bench/bench_raft.cpp` for reproducible local measurements. Actual results depend on compiler, allocator, storage backend, CPU frequency policy, and network / loopback environment.

Full guide: [Raft](docs/en/protocols/raft.md). Embedding guide for host projects
with overlapping dependencies: [Third-party Dependency Integration](docs/en/advanced/thirdparty-dependency-integration.md).

### HTTP / gRPC Performance
Windows Release benchmark on Intel Core i9-14900K, Visual Studio 2026, IOCP, local loopback, multicore mode (`mc:16/16`):

| Benchmark | Command | Throughput |
|----------|---------|------------|
| HTTP/1.1 cleartext | `bench_http.exe 1000 16` | ~117.69K req/s |
| HTTP/2 h2c | `bench_http.exe 1000 16` | ~100.66K req/s |
| HTTPS/1.1 | `bench_http.exe 1000 16` | ~41.54K req/s |
| HTTPS/2 | `bench_http.exe 1000 16` | ~41.24K req/s |
| WebSocket echo | `bench_ws.exe 1000 16` | ~290.00K msg/s |
| WebSocket Secure echo | `bench_ws.exe 1000 16` | ~73.52K msg/s |
| gRPC unary over HTTP/2 h2c | `bench_grpc.exe 5000 16` | ~112.92K req/s |

The gRPC correctness suite includes Python `grpcio` cross-process interoperability tests in both directions. Results are local-loopback numbers and vary with CPU power policy, TLS library, worker count, and concurrent system load.

### MQTT Performance
Windows Release benchmark on Intel Core i9-14900K, Visual Studio 2026, IOCP, local loopback, 4 broker workers, 8 publishers, QoS 0, `write_batch=16`:

| Benchmark | Command | Result |
|----------|---------|--------|
| MQTT QoS0 broker/client burst | `bench_mqtt.exe 20000 8 clientburst multi` | avg ~128.18K msg/s, peak ~133.78K msg/s |

Five consecutive runs completed with `160000 ok, 0 failed` each. Broker metrics reached `routed=160000` and `delivered=160000` on every run.

### Middleware (HTTP)
CORS, JWT auth, rate limiter, gzip compress, body limit, request ID, access log, metrics, timeout, graceful shutdown, IP firewall, cache, health check, file upload, panic recovery

### Synchronization Primitives
`mutex`, `shared_mutex`, `semaphore`, `condition_variable` (all coroutine-aware), `channel<T>`, `wait_group`, `cancel_token`

### Utilities
- **Timers**: `async_sleep()` / `async_sleep_until()` are convenience wrappers, `with_timeout()` targets cancellable `task<std::expected<...>>` operations
- **Buffer**: Endianness-aware readers/writers, buffer pool
- **Logging**: `std::format`-based logger (no external dependency)
- **Crash dump**: Platform-native minidump (Windows) / signal handler (Unix)
- **Async file I/O**: IOCP-backed (Windows)

## Quick Start

### Build Requirements

**CMake 4.0+** is required for C++23 module support.

**Windows**: Latest Visual Studio 2026 with C++23 modules enabled.

**Linux**: clang-21 with libc++ and liburing-dev installed.
```bash
wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh
sudo ./llvm.sh 21 all
sudo apt install libc++-21-dev libc++abi-21-dev liburing-dev
```

**macOS**: Homebrew LLVM 21+ (system clang does not support C++23 modules).
```bash
brew install llvm ninja cmake
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"  # Apple Silicon
export PATH="/usr/local/opt/llvm/bin:$PATH"      # Intel Mac
```

### Clone and Build

The repository supports three common build paths:

- **Submodule/local build**: best for developing this repository with the Git submodules under `3rdparty`.
- **vcpkg manifest build**: lets vcpkg own the dependencies; on Windows + VS 2026 use the included `x64-windows-vs2026` overlay triplet.
- **Conan build/package**: supports Conan-based distribution and reuse; `conan create` validates recipe export, isolated build, and packaging.

```bash
# Clone the repository
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod

# Initialize submodules (required for third-party dependencies)
git submodule update --init --recursive

# Build
cmake -B build -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build

# Build every cnetmod target explicitly
cmake --build build --target cnetmod_build_all

# Visual Studio generators with C++ modules: use single-node MSBuild
cmake --build build --target cnetmod_build_all --config Debug
```

### Build with vcpkg

The repository includes `vcpkg.json`, so a user with vcpkg can let manifest mode
install the supported third-party dependencies:

```bash
cmake -B build-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build-vcpkg --target cnetmod_build_all
```

On Windows with multiple Visual Studio versions installed, force Visual Studio
2026 with the included overlay triplet:

```bat
set VCPKG_ROOT=<path-to-vcpkg>
set VCPKG_VISUAL_STUDIO_PATH=<path-to-Visual-Studio-2026>

:: Optional: move vcpkg caches off the C drive when space is limited.
set X_VCPKG_REGISTRIES_CACHE=%USERPROFILE%\.cache\vcpkg\registries
set VCPKG_DOWNLOADS=%USERPROFILE%\.cache\vcpkg\downloads

%VCPKG_ROOT%\vcpkg.exe install --triplet x64-windows-vs2026 ^
  --overlay-triplets=cmake\vcpkg-triplets

cmake -S . -B build-vcpkg-vs2026 -G"Visual Studio 18 2026" ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-vs2026 ^
  -DVCPKG_OVERLAY_TRIPLETS=cmake/vcpkg-triplets

cmake --build build-vcpkg-vs2026 --config Release --target cnetmod_build_all
```

cnetmod first reuses dependencies exposed by the active toolchain or parent
project, then falls back to bundled `3rdparty` copies when they exist. `pugixml`
is kept as a normal Git submodule; package-manager builds should prefer the
package target and only fall back to that submodule when no system package is
available.

### Build with Conan

The repository also includes a Conan 2 recipe:

```bash
conan install . --output-folder=build-conan --build=missing \
  -s build_type=Release -s compiler.cppstd=23
cmake --preset conan-default
cmake --build --preset conan-release --target cnetmod_core
```

For Visual Studio 2026, use Conan 2.30+ and CMake 4.2+ so MSVC 195 and the
`Visual Studio 18 2026` generator are recognized:

```bat
:: Optional: move Conan cache and temp files off the C drive when space is limited.
set CONAN_HOME=%USERPROFILE%\.conan2-vs2026
set TEMP=%USERPROFILE%\.cache\build-tmp
set TMP=%USERPROFILE%\.cache\build-tmp

conan --version
conan install . --output-folder=build-conan-vs2026 --build=missing ^
  -s build_type=Release ^
  -s compiler=msvc -s compiler.version=195 ^
  -s compiler.runtime=dynamic -s compiler.runtime_type=Release ^
  -s compiler.cppstd=23 ^
  -c tools.cmake.cmaketoolchain:generator="Visual Studio 18 2026"

cmake --preset conan-default
cmake --build --preset conan-release --target cnetmod_core

:: Optional: validate recipe export, isolated build, and packaging
conan create . --build=missing -pr:h vs2026 -pr:b vs2026
```

The default Conan recipe installs ConanCenter packages for `jwt-cpp`,
`nlohmann_json`, `pugixml`, `libnghttp2`, `leveldb`, `openssl`, and `zlib`.
`mimalloc` is enabled by default and can be disabled with
`-o cnetmod/*:with_mimalloc=False`. `stdexec` is normally taken from
`3rdparty/stdexec`; if your Conan remote provides the upstream `p2300` package,
enable `-o cnetmod/*:with_stdexec_package=True`.

The build system auto-detects standard library module paths for MSVC and libc++. On Windows, install the latest Visual Studio 2026 and use the default auto-detected MSVC module paths. If detection fails on Linux/macOS, manually specify:
```bash
# Linux/macOS with clang
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/lib/llvm-21/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/lib/llvm-21/include/c++/v1
```

### Examples

**Echo Server** (TCP + coroutines):
```cpp
import cnetmod;
using namespace cnetmod;

task<void> handle_client(tcp_socket client) {
    char buf[1024];
    while (true) {
        int n = co_await client.async_recv(buf, sizeof(buf));
        if (n <= 0) break;
        co_await client.async_send(buf, n);
    }
}

task<void> server(io_context& ctx) {
    tcp_acceptor acceptor(ctx, 8080);
    while (true) {
        auto [client, addr] = co_await acceptor.async_accept();
        spawn(ctx, handle_client(std::move(client)));
    }
}

int main() {
    net_init guard;  // RAII for WSAStartup on Windows
    io_context ctx;
    spawn(ctx, server(ctx));
    ctx.run();
}
```

**MQTT Broker + Client**:
```cpp
import cnetmod.protocol.mqtt;

// Start broker
mqtt::broker brk(ctx);
brk.set_options({.port = 1883});
brk.listen();
spawn(ctx, brk.run());

// Async client
mqtt::client cli(ctx);
co_await cli.connect({.host = "127.0.0.1", .port = 1883, .version = mqtt::protocol_version::v5});
co_await cli.subscribe("sensor/#", mqtt::qos::at_least_once);
cli.on_message([](const mqtt::publish_message& msg) {
    std::println("topic={} payload={}", msg.topic, msg.payload);
});
co_await cli.publish("sensor/temp", "22.5", mqtt::qos::exactly_once);
co_await cli.disconnect();
```

**SOCKS5 Proxy**:
```cpp
import cnetmod.protocol.socks5;

// SOCKS5 Server
socks5::server_config config;
config.allow_no_auth = true;
config.allow_username_password = true;
config.add_user("user", "pass");

socks5::server proxy(ctx, config);
co_await proxy.listen(1080);
spawn(ctx, proxy.run());

// SOCKS5 Client
socks5::client client(ctx);
co_await client.connect("127.0.0.1", 1080);
co_await client.authenticate();  // No auth or username/password
co_await client.connect_to("example.com", 80);

// Now use the tunneled connection
co_await client.send("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
auto response = co_await client.recv(4096);
```

**Modbus Protocol**:
```cpp
import cnetmod.protocol.modbus;

// TCP Client
modbus::tcp_client client(ctx);
co_await client.connect("192.168.1.100", 502);

auto req = modbus::request_builder()
    .unit_id(1)
    .read_holding_registers(0, 10)
    .build();

auto resp = co_await client.execute(req);

// RTU Server (Serial)
modbus::memory_data_store store;
modbus::rtu_server_config config;
config.port_name = "COM3";  // Windows or "/dev/ttyUSB0" (Linux)
config.baudrate = 9600;
config.unit_id = 1;

modbus::rtu_server server(ctx, store);
co_await server.start(config);
```

**CoAP over UDP**:
```cpp
import cnetmod.protocol.coap;

using namespace cnetmod;

coap::udp_server server(ctx);
auto listen = server.listen("0.0.0.0", coap::default_port);
if (!listen) {
    throw std::system_error(listen.error());
}

server.route(coap::method::get, "/sensors/temp",
    [](const coap::inbound_request& req, const endpoint&) -> task<coap::message> {
        auto resp = coap::make_response(req.request, coap::response_code::content);
        resp.add_uint_option(coap::option_number::content_format,
            static_cast<std::uint16_t>(coap::content_format::text_plain));
        std::string body = "22.5";
        resp.payload.assign(reinterpret_cast<const std::byte*>(body.data()),
            reinterpret_cast<const std::byte*>(body.data() + body.size()));
        co_return resp;
    });
spawn(ctx, server.run());

coap::udp_client client(ctx);
auto remote = co_await client.resolve_endpoint("127.0.0.1");
if (remote) {
    auto response = co_await client.get(*remote, "/sensors/temp");
}
```

**Timer**:
```cpp
task<void> delayed_task(io_context& ctx) {
    co_await async_sleep(ctx, 1s);
    std::cout << "1 second elapsed\n";
}
```

**Channel** (producer-consumer):
```cpp
channel<int> ch(10);
spawn(ctx, producer(ch));  // sends 0..9
spawn(ctx, consumer(ch));  // receives and prints

task<void> producer(channel<int>& ch) {
    for (int i = 0; i < 10; ++i) co_await ch.send(i);
    ch.close();
}
```

**MySQL ORM** (model mapping + CRUD + migration + advanced features):
```cpp
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

// Define model with advanced features
struct User {
    std::int64_t                id         = 0;
    std::string                 name;
    std::optional<std::string>  email;
    std::int32_t                version    = 0;  // Optimistic lock
    std::int32_t                deleted    = 0;  // Soft delete
    std::time_t                 created_at = 0;  // Auto-fill on insert
    std::time_t                 updated_at = 0;  // Auto-fill on insert/update
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id,         "id",         bigint,    PK | AUTO_INC),
    CNETMOD_FIELD(name,       "name",       varchar),
    CNETMOD_FIELD(email,      "email",      varchar,   NULLABLE),
    CNETMOD_FIELD(version,    "version",    int_,      VERSION),
    CNETMOD_FIELD(deleted,    "deleted",    tinyint,   LOGIC_DELETE),
    CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", timestamp, FILL_INSERT_UPDATE)
)

task<void> demo(mysql::client& cli) {
    orm::db_session db(cli);

    // DDL — create / drop / sync_schema (auto-migration)
    co_await db.create_table<User>();
    co_await orm::sync_schema<User>(cli);  // detects diff and applies ALTER TABLE

    // INSERT (auto_increment ID auto-filled)
    User u; u.name = "Alice"; u.email = "a@b.com";
    co_await db.insert(u);           // u.id is now set
    co_await db.insert_many(batch);  // batch insert

    // SELECT — find_all / find_by_id / query builder
    auto all = co_await db.find_all<User>();
    auto one = co_await db.find_by_id<User>(param_value::from_int(1));
    auto top = co_await db.find(
        orm::select<User>()
            .where("`name` = {}", {param_value::from_string("Alice")})
            .order_by("`id` DESC")
            .limit(10).offset(0)
    );

    // UPDATE — by PK
    u.name = "Bob";
    co_await db.update(u);

    // DELETE — by model / by ID / conditional
    co_await db.remove(u);
    co_await db.remove_by_id<User>(param_value::from_int(1));
    co_await db.remove(orm::delete_of<User>()
        .where("`name` = {}", {param_value::from_string("test")}));
}

// BaseMapper — Generic CRUD interface
task<void> base_mapper_demo(mysql::client& cli) {
    base_mapper<User> mapper(cli);
    
    // QueryWrapper — Fluent query builder
    query_wrapper<User> wrapper;
    wrapper.eq("status", 1)
           .like("name", "%Alice%")
           .gt("age", 18)
           .order_by_desc("created_at")
           .limit(10);
    
    auto users = co_await mapper.select_list(wrapper);
    
    // Pagination
    auto page = co_await mapper.select_page(1, 10, wrapper);
    std::println("Page {}/{}, Total: {}", 
        page.current_page, page.total_pages, page.total);
}

// MyBatis-Plus style XML Mapper — Dynamic SQL
task<void> xml_mapper_demo(mysql::client& cli) {
    mapper_registry registry;
    registry.load_file("mappers/user_mapper.xml");
    
    mapper_session session(cli, registry);
    
    // Execute query with dynamic conditions
    auto result = co_await session.query<User>("UserMapper.findByCondition",
        param_context::from_map({
            {"name", param_value::from_string("Alice")},
            {"status", param_value::from_int(1)}
        }));
}
```

ID strategies — UUID v4 and Snowflake are built-in:
```cpp
struct Tag {
    orm::uuid    id;
    std::string  name;
};
CNETMOD_MODEL(Tag, "tags",
    CNETMOD_FIELD(id,   "id",   char_, UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name, "name", varchar)
)

struct Event {
    std::int64_t  id = 0;
    std::string   title;
};
CNETMOD_MODEL(Event, "events",
    CNETMOD_FIELD(id,    "id",    bigint, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY),
    CNETMOD_FIELD(title, "title", varchar)
)

// Snowflake requires a generator
orm::snowflake_generator sf(/*machine_id=*/1);
orm::db_session db(cli, sf);
co_await db.insert(event);  // event.id auto-generated
```

See `examples/` for complete demos including `http_demo`, `http2_demo`, `ws_demo`, `mqtt_demo`, `mysql_crud`, `mysql_orm`, `redis_client`, `redis_cluster`, `oss_shared_storage`, `modbus_demo`, `multicore_http`, `ssl_echo_server`, and more.

## Architecture

**Module structure**: Pure C++23 module interfaces (`.cppm`) with no headers. Platform-specific implementations in `.cpp` files selected via CMake.

```
cnetmod.core          — socket, buffer, address, error, log, dns, ssl, serial_port
cnetmod.coro          — task, spawn, channel, mutex, semaphore, timer, cancel
cnetmod.io            — io_context + platform backends (iocp, io_uring, epoll, kqueue)
cnetmod.executor      — async_op, server_context, scheduler, stdexec bridge
cnetmod.protocol.tcp  — TCP acceptor/connector
cnetmod.protocol.udp  — UDP async I/O
cnetmod.protocol.http — HTTP/1.1 + HTTP/2 server, router, middleware pipeline, ALPN negotiation
cnetmod.protocol.websocket — WebSocket server
cnetmod.protocol.socks5 — SOCKS5 proxy client + server
cnetmod.protocol.mqtt — MQTT broker + client (v3.1.1 / v5.0)
cnetmod.protocol.mysql — MySQL async client + ORM
cnetmod.protocol.redis — Redis async client
cnetmod.protocol.raft — Raft replicated state machine, storage, transport, runtime, membership, snapshots
cnetmod.protocol.modbus — Modbus TCP/UDP/RTU client + server
cnetmod.protocol.coap — CoAP UDP client + server, datagram codec, resource router
cnetmod.protocol.openai — OpenAI API client
cnetmod.protocol.http.middleware.*  — HTTP middleware components
cnetmod.utils         — Protocol conversion utilities (endian, CRC, hex, register conversion)
```

**Scheduler/executor**: `io_context` provides `post(coroutine_handle<>)` for thread-safe task submission. Platform-specific `wake()` implementations:
- Windows: `PostQueuedCompletionStatus` with sentinel key
- Linux: Non-blocking pipe + io_uring read
- macOS/epoll: eventfd/pipe drain triggers

**Coroutine primitives**: `task<T>` uses symmetric transfer for tail-call optimization. `spawn()` bridges eager coroutines to the scheduler via `detached_task`.

**Async operations**: RAII-based async_op base class with platform-specific overlap/submission tracking. Completion callbacks resume awaiting coroutines via `post()`.

## Known Issues

### clang module dependency visibility

When adding new `import` dependencies between modules, clang may report errors like:

> declaration of 'X' must be imported from module 'Y' before it is required

Ensure the module that exports the needed symbol is explicitly imported. Unlike headers, modules have strict visibility — transitive imports are not automatically visible.

## Design Rationale

**Why modules?** Zero-cost header-free build model. Reduced compile times and cleaner API surface. Aligns with C++23 standard library direction.

**Why coroutines?** Zero-overhead async/await without callback hell. Stackless coroutines compile to state machines with optimal performance.

**Why io_uring/IOCP/kqueue?** Platform-native async I/O delivers best performance. io_uring avoids syscall overhead. IOCP is battle-tested for Windows servers. kqueue is the only option for macOS.

**Why stdexec?** De facto standard for sender/receiver. Enables composition with other async libraries. Structured concurrency via `async_scope`. Used for blocking operation offloading (`blocking_invoke`).

## Project Status

cnetmod is a modern C++23 network library showcasing the power of modules and coroutines. It provides production-grade implementations of HTTP/1.1 & HTTP/2, MQTT, MySQL, WebSocket, Modbus, CoAP, and more, all built with zero-overhead async/await.

The library demonstrates that C++23 modules are ready for real-world use, with full cross-platform support on Linux, macOS, and Windows.

## License

MIT License. See LICENSE file for details.
