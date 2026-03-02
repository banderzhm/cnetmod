# cnetmod

Cross-platform asynchronous network library using C++23 modules and native coroutines.

[![Linux Build](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml)
[![macOS Build](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml)
[![Windows Build](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml)

English | [简体中文](README_zh.md)

## Platform Support

| Platform | I/O Engine | Compiler | Status |
|----------|-----------|----------|--------|
| Windows | IOCP | MSVC 2022 17.12+ | ✅ |
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
- **MQTT v3.1.1 / v5.0**: Full broker + async client — QoS 0/1/2, retained messages, will, session resume, shared subscriptions, topic alias, auto-reconnect; sync client wrapper
- **MySQL**: Async client with prepared statements, connection pool, pipeline, transaction management, ORM (CRUD / migration / query builder / MyBatis-Plus style XML mappers / BaseMapper / pagination / soft delete / optimistic lock / multi-tenant / cache)
- **Redis**: Async client with RESP protocol, connection pool
- **OpenAI**: Async API client (chat completions, etc.)

### Middleware (HTTP)
CORS, JWT auth, rate limiter, gzip compress, body limit, request ID, access log, metrics, timeout, graceful shutdown, IP firewall, cache, health check, file upload, panic recovery

### Synchronization Primitives
`mutex`, `shared_mutex`, `semaphore`, `condition_variable` (all coroutine-aware), `channel<T>`, `wait_group`, `cancel_token`

### Utilities
- **Timers**: `async_sleep()`, `async_sleep_until()`, `with_timeout()` for cancellable operations
- **Buffer**: Endianness-aware readers/writers, buffer pool
- **Logging**: `std::format`-based logger (no external dependency)
- **Crash dump**: Platform-native minidump (Windows) / signal handler (Unix)
- **Async file I/O**: IOCP-backed (Windows)

## Quick Start

### Build Requirements

**CMake 4.0+** is required for C++23 module support.

**Windows**: Visual Studio 2022 17.12+ with C++23 modules enabled.

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

```bash
# Clone the repository
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod

# Initialize submodules (required for third-party dependencies)
git submodule update --init --recursive

# Build
cmake -B build -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

The build system auto-detects standard library module paths for MSVC and libc++. If detection fails, manually specify:
```bash
# Linux/macOS with clang
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/lib/llvm-21/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/lib/llvm-21/include/c++/v1

# Windows MSVC
cmake -B build \
  -DLIBCXX_MODULE_DIRS="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/modules"
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

See `examples/` for complete demos including `http_demo`, `http2_demo`, `ws_demo`, `mqtt_demo`, `mysql_crud`, `mysql_orm`, `redis_client`, `multicore_http`, `ssl_echo_server`, and more.

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
cnetmod.protocol.mqtt — MQTT broker + client (v3.1.1 / v5.0)
cnetmod.protocol.mysql — MySQL async client + ORM
cnetmod.protocol.redis — Redis async client
cnetmod.protocol.openai — OpenAI API client
cnetmod.middleware.*  — HTTP middleware components
```

**Scheduler/executor**: `io_context` provides `post(coroutine_handle<>)` for thread-safe task submission. Platform-specific `wake()` implementations:
- Windows: `PostQueuedCompletionStatus` with sentinel key
- Linux: Non-blocking pipe + io_uring read
- macOS/epoll: eventfd/pipe drain triggers

**Coroutine primitives**: `task<T>` uses symmetric transfer for tail-call optimization. `spawn()` bridges eager coroutines to the scheduler via `detached_task`.

**Async operations**: RAII-based async_op base class with platform-specific overlap/submission tracking. Completion callbacks resume awaiting coroutines via `post()`.

## Known Issues

### MSVC error C1605: object file size exceeds 4 GB limit

When building with MSVC in Debug mode, the compiler may emit:

> **fatal error C1605**: 编译器限制: 对象文件大小不能超过 4 GB

This is a **known MSVC bug** with C++23 modules. Microsoft has acknowledged this issue in their Developer Community: [C1605: C++23 Modules Exceed 4 GB Object File Limit](https://developercommunity.visualstudio.com/t/C1605:-C23-Modules-Exceed-4-GB-Object-/11048477).

The problem occurs because the compiler incorrectly generates oversized object files when processing C++23 modules with heavy template instantiation (especially `std` module + `std::format` + protocol codecs). MSVC's COFF object format has a hard 4 GB limit.

**Recommended solutions:**
1. **Use Release/RelWithDebInfo builds** — optimized builds produce significantly smaller object files and avoid this compiler bug.
2. **Build with clang on WSL/Linux** — clang + libc++ uses ELF objects which have no 4 GB limit. This is the recommended development workflow.

**Note**: This is a compiler bug, not a limitation of C++23 modules or this library. We are waiting for Microsoft to fix this issue. See [MSVC_C1605_Issue.md](MSVC_C1605_Issue.md) for detailed analysis.

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

cnetmod is a modern C++23 network library showcasing the power of modules and coroutines. It provides production-grade implementations of HTTP/1.1 & HTTP/2, MQTT, MySQL, WebSocket, and more, all built with zero-overhead async/await.

The library demonstrates that C++23 modules are ready for real-world use, with full cross-platform support on Linux, macOS, and Windows.

## License

MIT License. See LICENSE file for details.
