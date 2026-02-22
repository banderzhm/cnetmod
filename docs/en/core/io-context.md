# I/O Context

The `io_context` is the heart of cnetmod's asynchronous I/O system. It provides an event loop, task scheduling, and platform-specific I/O multiplexing.

## Overview

`io_context` responsibilities:
- **Event loop**: Waits for I/O events and timers
- **Task scheduling**: Executes coroutines when I/O completes
- **Thread-safe posting**: Submit tasks from any thread
- **Platform abstraction**: Unified API across Windows/Linux/macOS

## Basic Usage

### Creating and Running

```cpp
import cnetmod;
using namespace cnetmod;

int main() {
    // Create I/O context
    io_context ctx;
    
    // Schedule work
    spawn(ctx, my_async_task(ctx));
    
    // Run event loop (blocks until stopped)
    ctx.run();
}
```

### Stopping the Event Loop

```cpp
task<void> shutdown_task(io_context& ctx) {
    co_await async_sleep(ctx, 10s);
    ctx.stop();  // Stop the event loop
}

int main() {
    io_context ctx;
    spawn(ctx, shutdown_task(ctx));
    ctx.run();  // Returns after 10 seconds
}
```

## Platform Backends

cnetmod uses the best I/O mechanism for each platform:

| Platform | Backend | Description |
|----------|---------|-------------|
| Windows | **IOCP** | I/O Completion Ports - high-performance async I/O |
| Linux | **io_uring** | Modern async I/O with kernel submission queue |
| Linux | **epoll** | Fallback if io_uring unavailable |
| macOS | **kqueue** | BSD kernel event notification |

### Backend Selection

Automatic at compile time:
```cpp
#ifdef CNETMOD_HAS_IOCP
    // Windows IOCP implementation
#elif defined(CNETMOD_HAS_IO_URING)
    // Linux io_uring implementation
#elif defined(CNETMOD_HAS_KQUEUE)
    // macOS kqueue implementation
#elif defined(CNETMOD_HAS_EPOLL)
    // Linux epoll implementation
#endif
```

## Thread Model

### Single-Threaded

One `io_context` per thread:

```cpp
int main() {
    io_context ctx;
    
    // All tasks run on this thread
    spawn(ctx, task1(ctx));
    spawn(ctx, task2(ctx));
    spawn(ctx, task3(ctx));
    
    ctx.run();  // Runs all tasks on current thread
}
```

### Multi-Threaded

Use `server_context` for multi-core:

```cpp
int main() {
    // 1 accept thread + 4 worker threads
    server_context srv_ctx(4);
    
    http::server srv(srv_ctx.get_io_context());
    srv.listen(8080);
    
    srv_ctx.run();  // Blocks until stopped
}
```

**Architecture**:
```
Accept Thread (io_context)
    ↓
    ├─→ Worker 1 (io_context)
    ├─→ Worker 2 (io_context)
    ├─→ Worker 3 (io_context)
    └─→ Worker 4 (io_context)
```

## Task Scheduling

### `post()` - Thread-Safe Submission

Submit a coroutine from any thread:

```cpp
void worker_thread(io_context& ctx) {
    // Called from different thread
    ctx.post([&]() -> task<void> {
        co_await do_work(ctx);
    }().handle());
}

int main() {
    io_context ctx;
    
    std::thread worker([&] { worker_thread(ctx); });
    
    ctx.run();
    worker.join();
}
```

### `spawn()` - Convenient Wrapper

```cpp
spawn(ctx, my_task(ctx));

// Equivalent to:
ctx.post(my_task(ctx).handle());
```

### Wake Mechanism

How `post()` wakes the event loop:

**Windows (IOCP)**:
```cpp
PostQueuedCompletionStatus(iocp_handle, 0, WAKE_KEY, nullptr);
```

**Linux (io_uring)**:
```cpp
// Write to pipe, io_uring reads it
write(wake_pipe[1], &dummy, 1);
```

**macOS (kqueue)**:
```cpp
// Trigger eventfd
uint64_t value = 1;
write(eventfd, &value, sizeof(value));
```

## Event Loop Internals

### The `run()` Loop

Simplified pseudocode:

```cpp
void io_context::run() {
    while (!stopped_) {
        // 1. Process ready tasks
        while (auto* task = ready_queue_.pop()) {
            task->resume();
        }
        
        // 2. Wait for I/O events (platform-specific)
        auto events = wait_for_events(timeout);
        
        // 3. Process completed I/O
        for (auto& event : events) {
            auto* op = get_async_op(event);
            op->complete();  // Resumes awaiting coroutine
        }
    }
}
```

### Timeout Handling

```cpp
// Wait with timeout
auto timeout = calculate_next_timer();
wait_for_events(timeout);

// Check expired timers
process_expired_timers();
```

## Async Operations

### How I/O Works

1. **Initiate**: Submit I/O operation to kernel
2. **Suspend**: Coroutine suspends, event loop continues
3. **Complete**: Kernel signals completion
4. **Resume**: Event loop resumes coroutine

```cpp
task<int> read_example(tcp_socket& sock) {
    char buffer[1024];
    
    // 1. Initiate read
    int n = co_await sock.async_recv(buffer, sizeof(buffer));
    //         ↑
    //         Suspends here, returns to event loop
    
    // 2. Event loop waits for completion
    // 3. Kernel signals data ready
    // 4. Coroutine resumes here
    
    std::println("Read {} bytes", n);
    co_return n;
}
```

### Platform-Specific Implementation

**Windows (IOCP)**:
```cpp
struct async_recv_op : async_op {
    OVERLAPPED overlapped{};
    
    void start() {
        WSARecv(socket, &buffer, 1, nullptr, &flags, &overlapped, nullptr);
    }
    
    void complete() {
        // Called by IOCP completion
        ctx_.post(awaiting_coroutine_);
    }
};
```

**Linux (io_uring)**:
```cpp
struct async_recv_op : async_op {
    io_uring_sqe* sqe;
    
    void start() {
        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe, socket, buffer, size, 0);
        io_uring_sqe_set_data(sqe, this);
        io_uring_submit(&ring);
    }
    
    void complete() {
        // Called when CQE received
        ctx_.post(awaiting_coroutine_);
    }
};
```

## Timers

### `async_sleep()`

Sleep for a duration:

```cpp
task<void> delayed_task(io_context& ctx) {
    std::println("Starting...");
    co_await async_sleep(ctx, 2s);
    std::println("2 seconds later");
}
```

### `async_sleep_until()`

Sleep until a specific time point:

```cpp
task<void> scheduled_task(io_context& ctx) {
    auto target = std::chrono::system_clock::now() + 1h;
    co_await async_sleep_until(ctx, target);
    std::println("One hour has passed");
}
```

### `with_timeout()`

Add timeout to any operation:

```cpp
task<void> timeout_example(io_context& ctx) {
    try {
        auto result = co_await with_timeout(ctx, slow_operation(), 5s);
        std::println("Success: {}", result);
    } catch (const timeout_error&) {
        std::println("Operation timed out");
    }
}
```

### Timer Implementation

Timers use a priority queue (min-heap) sorted by expiration time:

```cpp
struct timer_entry {
    time_point expiration;
    std::coroutine_handle<> awaiting;
};

std::priority_queue<timer_entry> timers_;

// In event loop:
auto next_expiration = timers_.top().expiration;
auto timeout = next_expiration - now();
wait_for_events(timeout);
```

## Performance Characteristics

### Event Loop Overhead

- **IOCP**: ~1-2 μs per event
- **io_uring**: ~0.5-1 μs per event
- **epoll**: ~1-2 μs per event
- **kqueue**: ~1-2 μs per event

### Scalability

Tested configurations:
- **Connections**: 100K+ concurrent connections
- **Throughput**: 1M+ requests/sec (HTTP plaintext)
- **Latency**: p50: 0.5ms, p99: 2ms

### Memory Usage

Per `io_context`:
- Base: ~1 KB
- Per connection: ~200 bytes (socket + async_op)
- Per timer: ~32 bytes

## Thread Safety

### Thread-Safe Operations

- ✅ `post()` - Can be called from any thread
- ✅ `stop()` - Can be called from any thread
- ✅ `wake()` - Internal, thread-safe

### Thread-Unsafe Operations

- ❌ `run()` - Must be called from one thread only
- ❌ Socket operations - Must be on same `io_context` thread
- ❌ Coroutine resume - Must be on same thread

### Safe Multi-Threading Pattern

```cpp
class thread_safe_server {
    io_context ctx_;
    std::thread thread_;
    
public:
    void start() {
        thread_ = std::thread([this] { ctx_.run(); });
    }
    
    void submit_task(task<void> t) {
        // Thread-safe: post from any thread
        spawn(ctx_, std::move(t));
    }
    
    void stop() {
        ctx_.stop();
        thread_.join();
    }
};
```

## Best Practices

### 1. One `io_context` Per Thread

```cpp
// GOOD: Each thread has its own io_context
std::vector<std::thread> threads;
for (int i = 0; i < 4; ++i) {
    threads.emplace_back([] {
        io_context ctx;
        // ... setup work ...
        ctx.run();
    });
}
```

### 2. Use `server_context` for Multi-Core

```cpp
// GOOD: Automatic load balancing
server_context srv_ctx(std::thread::hardware_concurrency());
```

### 3. Don't Block the Event Loop

```cpp
// BAD: Blocks event loop
task<void> bad_example(io_context& ctx) {
    std::this_thread::sleep_for(1s);  // Blocks!
}

// GOOD: Use async sleep
task<void> good_example(io_context& ctx) {
    co_await async_sleep(ctx, 1s);  // Doesn't block
}
```

### 4. Offload CPU-Intensive Work

```cpp
task<int> cpu_intensive(io_context& ctx) {
    // Offload to thread pool
    co_return co_await blocking_invoke(ctx, [] {
        return expensive_computation();
    });
}
```

### 5. Graceful Shutdown

```cpp
class application {
    io_context ctx_;
    std::atomic<bool> shutdown_requested_{false};
    
    task<void> shutdown_monitor() {
        while (!shutdown_requested_) {
            co_await async_sleep(ctx_, 100ms);
        }
        ctx_.stop();
    }
    
public:
    void run() {
        spawn(ctx_, shutdown_monitor());
        ctx_.run();
    }
    
    void request_shutdown() {
        shutdown_requested_ = true;
    }
};
```

## Debugging

### Enable Logging

```cpp
// Set log level
log::set_level(log::level::debug);

// Log I/O operations
log::debug("Socket {} recv {} bytes", socket.fd(), n);
```

### Check Event Loop Status

```cpp
// Is event loop running?
bool is_running = !ctx.stopped();

// Number of pending operations
size_t pending = ctx.pending_count();
```

### Detect Deadlocks

If `run()` never returns:
1. Check if all tasks are waiting for I/O
2. Verify timers are set correctly
3. Ensure `stop()` is called eventually

## Common Pitfalls

### 1. Forgetting to Call `run()`

```cpp
// BAD: Event loop never runs
int main() {
    io_context ctx;
    spawn(ctx, my_task(ctx));
    // Missing: ctx.run();
}
```

### 2. Blocking in Coroutines

```cpp
// BAD: Blocks event loop
task<void> bad(io_context& ctx) {
    std::this_thread::sleep_for(1s);  // Blocks all tasks!
}
```

### 3. Dangling References

```cpp
// BAD: ctx destroyed before tasks complete
void bad_function() {
    io_context ctx;
    spawn(ctx, long_running_task(ctx));
}  // ctx destroyed, tasks still running!
```

## Next Steps

- **[Coroutines](coroutines.md)** - Understand `task<T>` and `spawn()`
- **[Synchronization Primitives](sync-primitives.md)** - Mutex, channel, semaphore
- **[TCP/UDP Guide](../protocols/tcp-udp.md)** - Network programming basics
- **[Multi-Core Architecture](../advanced/multi-core.md)** - Scale to multiple cores
