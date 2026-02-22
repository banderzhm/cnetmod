# Getting Started with cnetmod

This guide will help you get up and running with cnetmod quickly.

## Prerequisites

- **C++23 compiler**: MSVC 2022 17.12+, clang-21+, or GCC 14+ (with module support)
- **CMake 4.0+**: Required for C++23 module build support
- **Platform-specific dependencies**:
  - Windows: Visual Studio 2022 with C++23 modules
  - Linux: liburing-dev (for io_uring support)
  - macOS: Homebrew LLVM

## Installation

### 1. Clone the Repository

```bash
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

### 2. Platform-Specific Setup

#### Windows (MSVC)

```bash
# Open Visual Studio 2022 Developer Command Prompt
cmake -B build -G "Visual Studio 17 2022" -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

#### Linux (clang + libc++)

```bash
# Install dependencies
sudo apt install clang-21 libc++-21-dev libc++abi-21-dev liburing-dev

# Build
cmake -B build -DCMAKE_CXX_COMPILER=clang++-21 -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

#### macOS (Homebrew LLVM)

```bash
# Install dependencies
brew install llvm ninja cmake

# Set up environment
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"  # Apple Silicon
# export PATH="/usr/local/opt/llvm/bin:$PATH"   # Intel Mac

# Build
cmake -B build -G Ninja -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

## Your First Program

### Echo Server

Create a simple TCP echo server:

```cpp
import cnetmod;
using namespace cnetmod;

task<void> handle_client(tcp_socket client) {
    char buffer[1024];
    while (true) {
        int n = co_await client.async_recv(buffer, sizeof(buffer));
        if (n <= 0) break;
        co_await client.async_send(buffer, n);
    }
}

task<void> server(io_context& ctx) {
    tcp_acceptor acceptor(ctx, 8080);
    std::println("Server listening on port 8080");
    
    while (true) {
        auto [client, addr] = co_await acceptor.async_accept();
        std::println("Client connected: {}", addr.to_string());
        spawn(ctx, handle_client(std::move(client)));
    }
}

int main() {
    net_init guard;  // Initialize network (WSAStartup on Windows)
    io_context ctx;
    spawn(ctx, server(ctx));
    ctx.run();
}
```

### HTTP Server

Create a simple HTTP server with routing:

```cpp
import cnetmod;
import cnetmod.protocol.http;
using namespace cnetmod;

int main() {
    net_init guard;
    io_context ctx;
    
    http::server srv(ctx);
    
    // Add routes
    srv.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello, World!");
        co_return;
    });
    
    srv.get("/api/users/:id", [](http::request_context& ctx) -> task<void> {
        auto id = ctx.param("id");
        ctx.resp().json({{"id", id}, {"name", "User " + std::string(id)}});
        co_return;
    });
    
    srv.listen(8080);
    std::println("HTTP server listening on http://localhost:8080");
    ctx.run();
}
```

### Timer Example

Use async timers:

```cpp
import cnetmod;
using namespace cnetmod;

task<void> delayed_task(io_context& ctx) {
    std::println("Starting task...");
    co_await async_sleep(ctx, 2s);
    std::println("2 seconds elapsed!");
}

int main() {
    io_context ctx;
    spawn(ctx, delayed_task(ctx));
    ctx.run();
}
```

## Next Steps

- Read the [Architecture Overview](architecture.md) to understand how cnetmod works
- Explore [Core Concepts](core/coroutines.md) to learn about coroutines and I/O
- Check out [Examples](examples.md) for more complete applications
- Learn about [HTTP Middleware](middleware/http-middleware.md) for building web services

## Common Issues

### Module Path Detection Failed

If CMake cannot find standard library modules, manually specify paths:

```bash
# Linux/macOS
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/lib/llvm-21/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/lib/llvm-21/include/c++/v1

# Windows
cmake -B build \
  -DLIBCXX_MODULE_DIRS="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/modules"
```

### MSVC Error C1605 (Object File Too Large)

See [MSVC_C1605_Issue.md](../MSVC_C1605_Issue.md) for workarounds. Use Release build or split large modules.

### Link Errors on Linux

Ensure you're using libc++ (not libstdc++):

```bash
cmake -B build -DCMAKE_CXX_FLAGS="-stdlib=libc++"
```

## Getting Help

- **GitHub Issues**: Report bugs or ask questions
- **Examples**: Check the `examples/` directory for working code
- **Documentation**: Browse the full docs at `docs/`
