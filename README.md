# cnetmod

Production-oriented C++23 asynchronous networking infrastructure built on native coroutines and platform-native I/O backends.

> In the age of AI-assisted programming, architectural maturity has leveled the differences in development difficulty; performance and efficiency are the only decisive benchmarks.

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

### AI Development Skills

The [`skill/`](skill/) directory contains the project-specific instructions for AI-assisted development. AI agents should read [`skill/SKILL.md`](skill/SKILL.md) first, then consult the topic-specific guidance under `skill/core`, `skill/coro`, `skill/database`, `skill/http`, `skill/infra`, `skill/protocols`, and `skill/security` as needed.

### Engineering Evidence

- **Architecture**: C++23 module interfaces with platform-specific implementations selected by CMake; native IOCP, io_uring, epoll, and kqueue backends share the same coroutine-facing APIs.
- **Verification**: GitHub Actions builds target Linux, macOS, and Windows configurations. The repository includes unit, protocol interoperability, benchmark, chaos, and restart-recovery test paths.
- **Reproducibility**: Performance sections identify hardware, compiler, backend, workload, and concurrency parameters, and point to local benchmark targets for verification.
- **Operational behavior**: Protocol clients and brokers include reconnect, retry, flow-control, persistence, TLS/mTLS, and recovery mechanisms where applicable; asynchronous APIs use structured results and error codes.

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
- **SOCKS5**: Proxy protocol client and server — CONNECT, BIND, UDP ASSOCIATE commands; authentication methods (no auth, username/password, RFC 1961 GSSAPI provider callbacks); IPv4, IPv6, and domain name support
- **MQTT v3.1.1 / v5.0**: Full broker + async client — QoS 0/1/2, retained messages, will, session resume, shared subscriptions, topic alias, auto-reconnect; sync client wrapper
- **Kafka**: Async producer and consumer groups — metadata discovery, API-version negotiation, record batches, gzip/LZ4 compression, idempotent and transactional production, cooperative-sticky rebalancing, manual offset commits, SASL/TLS, retry and reconnect handling
- **AMQP 0-9-1**: RabbitMQ-compatible async client — channels, durable exchanges/queues/bindings, publisher confirms, QoS/prefetch, ACK/NACK, transactions, heartbeats, automatic reconnection and topology recovery, SASL/TLS
- **AMQP 1.0**: Artemis-compatible async client — SASL/TLS connections, sessions, sender/receiver links, credit-based flow control, unsettled delivery outcomes, explicit settlement, transactions, reconnect and link recovery
- **MySQL**: Async client with prepared statements, connection pool, pipeline, transaction management, ORM (CRUD / migration / query builder / MyBatis-Plus style XML mappers / BaseMapper / pagination / soft delete / optimistic lock / multi-tenant / cache)
- **PostgreSQL**: Async TLS client with SCRAM-SHA-256/MD5 authentication, prepared queries, cancellation, and seamless reuse of the MySQL ORM model/query/session API
- **MongoDB**: Async BSON/OP_MSG client with TLS, SCRAM-SHA-256, capability negotiation, strict protocol limits, and correlated command execution
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
`nlohmann_json`, `pugixml`, `leveldb`, `openssl`, and `zlib`.
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
cnetmod.protocol.kafka — Kafka producer, consumer groups, offsets, idempotence, transactions
cnetmod.protocol.amqp091 — AMQP 0-9-1 / RabbitMQ client, channels, confirms, recovery
cnetmod.protocol.amqp10 — AMQP 1.0 client, sessions, links, flow control, settlement
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
- Linux io_uring: Non-blocking pipe + io_uring read
- Linux epoll: eventfd drain trigger
- macOS kqueue: pipe drain trigger

**Coroutine primitives**: `task<T>` uses symmetric transfer for tail-call optimization. `spawn()` bridges eager coroutines to the scheduler via `detached_task`.

**Async operations**: RAII-based async_op base class with platform-specific overlap/submission tracking. Completion callbacks resume awaiting coroutines via `post()`.

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
