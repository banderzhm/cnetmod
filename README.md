# cnetmod

Cross-platform asynchronous network library using C++23 modules and native coroutines.

[![Linux Build](https://github.com/yourusername/cnetmod/actions/workflows/linux-clang.yml/badge.svg)](https://github.com/yourusername/cnetmod/actions/workflows/linux-clang.yml)
[![macOS Build](https://github.com/yourusername/cnetmod/actions/workflows/macos-clang.yml/badge.svg)](https://github.com/yourusername/cnetmod/actions/workflows/macos-clang.yml)
[![Windows Build](https://github.com/yourusername/cnetmod/actions/workflows/windows-msvc.yml/badge.svg)](https://github.com/yourusername/cnetmod/actions/workflows/windows-msvc.yml)

## Platform Support

| Platform | I/O Engine | Compiler | Status |
|----------|-----------|----------|--------|
| Windows | IOCP | MSVC 2022 | ✅ |
| Linux | io_uring | clang-21 + libc++ | ✅ |
| macOS | kqueue | clang-21 + libc++ | ✅ |

## Features

**Core async runtime**: Native coroutine integration with `task<T>` and `spawn()` for fire-and-forget tasks. Platform-specific I/O context (`io_context`) with thread-safe `post()` for cross-thread scheduling.

**stdexec integration**: Built-in sender/receiver support. The scheduler exposes `schedule()` sender and can be used with `exec::async_scope` for structured concurrency.

**Network I/O**: TCP sockets with async accept/connect/read/write. Cross-platform socket RAII wrappers with move semantics.

**Synchronization primitives**: Coroutine-aware `mutex`, `condition_variable`, and `channel<T>` for producer-consumer patterns.

**Timers**: High-resolution timers with `async_sleep()` and `async_sleep_until()`. Platform backends: Windows multimedia timer, Linux/macOS steady_clock + heap.

**Buffer utilities**: Endianness-aware buffer readers/writers. Support for `hton`/`ntoh`, `htole`/`letoh`, and direct 16/32/64-bit integer serialization.

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

### Build

```bash
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

**Async File I/O** (Windows):
```cpp
async_file file("test.txt", file_mode::read);
char buf[4096];
int n = co_await file.async_read(buf, sizeof(buf), 0);
```

See `examples/` for complete demos: `echo_server.cpp`, `timer_demo.cpp`, `channel_demo.cpp`, `async_file.cpp`, `stdexec_bridge.cpp`.

## Architecture

**Module structure**: Pure C++23 module interfaces (`.cppm`) with no headers. Platform-specific implementations in `.cpp` files selected via CMake.

**Scheduler/executor**: `io_context` provides `post(coroutine_handle<>)` for thread-safe task submission. Platform-specific `wake()` implementations:
- Windows: `PostQueuedCompletionStatus` with sentinel key
- Linux: Non-blocking pipe + io_uring read
- macOS/epoll: eventfd/pipe drain triggers

**Coroutine primitives**: `task<T>` uses symmetric transfer for tail-call optimization. `spawn()` bridges eager coroutines to the scheduler via `detached_task`.

**Async operations**: RAII-based async_op base class with platform-specific overlap/submission tracking. Completion callbacks resume awaiting coroutines via `post()`.

## Design Rationale

**Why modules?** Zero-cost header-free build model. Reduced compile times and cleaner API surface. Aligns with C++23 standard library direction.

**Why coroutines?** Zero-overhead async/await without callback hell. Stackless coroutines compile to state machines with optimal performance.

**Why io_uring/IOCP/kqueue?** Platform-native async I/O delivers best performance. io_uring avoids syscall overhead. IOCP is battle-tested for Windows servers. kqueue is the only option for macOS.

**Why stdexec?** De facto standard for sender/receiver. Enables composition with other async libraries (libunifex, async++). Structured concurrency via `async_scope`.

## Project Status

This is a learning project exploring modern C++23 features. The API is unstable and not production-ready. Use at your own risk.

Missing features: UDP sockets, TLS/SSL, async DNS resolution, signal handling, multi-threaded I/O contexts.

## License

MIT License. See LICENSE file for details.
