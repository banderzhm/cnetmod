# Synchronization Primitives

cnetmod provides coroutine-aware synchronization primitives that suspend instead of blocking threads.

## Overview

All primitives are:
- **Coroutine-aware**: Suspend the coroutine, not the thread
- **Zero-overhead**: No thread blocking or context switching
- **FIFO ordering**: Fair scheduling for waiting coroutines
- **Exception-safe**: RAII guards for automatic cleanup

Available primitives:
- `mutex` - Exclusive lock
- `shared_mutex` - Reader-writer lock
- `semaphore` - Counting semaphore
- `channel<T>` - Go-style communication channel
- `wait_group` - Barrier for multiple tasks
- `cancel_token` - Cooperative cancellation

## Mutex

Exclusive lock for protecting shared resources.

### Basic Usage

```cpp
import cnetmod;
using namespace cnetmod;

mutex mtx;
int shared_counter = 0;

task<void> increment(io_context& ctx) {
    for (int i = 0; i < 1000; ++i) {
        co_await mtx.lock();
        ++shared_counter;
        mtx.unlock();
    }
}
```

### RAII Guard

```cpp
task<void> safe_increment(io_context& ctx) {
    auto guard = co_await mtx.scoped_lock();
    ++shared_counter;
    // Automatically unlocked when guard destroyed
}
```

### Try Lock

```cpp
task<void> try_lock_example() {
    if (mtx.try_lock()) {
        // Got the lock
        ++shared_counter;
        mtx.unlock();
    } else {
        // Lock not available
        co_await handle_busy();
    }
}
```

### Implementation Details

```cpp
class mutex {
    std::atomic<bool> locked_{false};
    std::mutex queue_mtx_;  // Protects waiter queue
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
    
public:
    auto lock() -> lock_awaitable;
    void unlock();
    bool try_lock();
};
```

**FIFO ordering**: Waiters are queued and resumed in order.

## Shared Mutex

Reader-writer lock allowing multiple readers or one writer.

### Basic Usage

```cpp
shared_mutex rw_mtx;
std::map<int, std::string> shared_data;

// Reader
task<std::string> read_data(int key) {
    co_await rw_mtx.lock_shared();
    auto value = shared_data[key];
    rw_mtx.unlock_shared();
    co_return value;
}

// Writer
task<void> write_data(int key, std::string value) {
    co_await rw_mtx.lock();
    shared_data[key] = std::move(value);
    rw_mtx.unlock();
}
```

### RAII Guards

```cpp
// Read lock
task<std::string> read_with_guard(int key) {
    auto guard = co_await rw_mtx.scoped_lock_shared();
    co_return shared_data[key];
}

// Write lock
task<void> write_with_guard(int key, std::string value) {
    auto guard = co_await rw_mtx.scoped_lock();
    shared_data[key] = std::move(value);
}
```

### Writer Priority

Writers have priority to prevent writer starvation:

```cpp
// State: 5 readers holding lock
// Writer arrives → blocks new readers
// Existing readers finish → writer gets lock
// New readers wait until writer finishes
```

### Implementation

```cpp
class shared_mutex {
    std::mutex mtx_;
    int state_ = 0;  // 0=free, >0=N readers, -1=writer
    
    waiter_node* write_head_ = nullptr;
    waiter_node* write_tail_ = nullptr;
    waiter_node* read_head_ = nullptr;
    waiter_node* read_tail_ = nullptr;
};
```

## Semaphore

Counting semaphore for resource limiting.

### Basic Usage

```cpp
semaphore sem(3);  // Allow 3 concurrent operations

task<void> limited_operation() {
    co_await sem.acquire();
    
    // Only 3 tasks can be here simultaneously
    co_await do_work();
    
    sem.release();
}
```

### RAII Guard

```cpp
task<void> safe_operation() {
    auto guard = co_await sem.scoped_acquire();
    co_await do_work();
    // Automatically released
}
```

### Try Acquire

```cpp
task<void> try_acquire_example() {
    if (sem.try_acquire()) {
        co_await do_work();
        sem.release();
    } else {
        // Resource not available
        co_await handle_busy();
    }
}
```

### Use Cases

**Connection Pool**:
```cpp
class connection_pool {
    semaphore sem_;
    std::vector<connection> connections_;
    
public:
    connection_pool(size_t size) : sem_(size) {
        for (size_t i = 0; i < size; ++i)
            connections_.push_back(create_connection());
    }
    
    task<connection> acquire() {
        co_await sem_.acquire();
        co_return get_available_connection();
    }
    
    void release(connection conn) {
        return_connection(conn);
        sem_.release();
    }
};
```

**Rate Limiting**:
```cpp
class rate_limiter {
    semaphore sem_;
    io_context& ctx_;
    
public:
    rate_limiter(io_context& ctx, int rate_per_second)
        : sem_(rate_per_second), ctx_(ctx) {
        spawn(ctx_, refill_loop());
    }
    
    task<void> acquire() {
        co_await sem_.acquire();
    }
    
private:
    task<void> refill_loop() {
        while (true) {
            co_await async_sleep(ctx_, 1s);
            sem_.release(rate_per_second_);
        }
    }
};
```

## Channel

Go-style channel for communication between coroutines.

### Basic Usage

```cpp
channel<int> ch(10);  // Buffered channel, capacity 10

// Producer
task<void> producer() {
    for (int i = 0; i < 100; ++i) {
        co_await ch.send(i);
    }
    ch.close();
}

// Consumer
task<void> consumer() {
    while (true) {
        auto value = co_await ch.recv();
        if (!value) break;  // Channel closed
        std::println("Received: {}", *value);
    }
}
```

### Unbuffered Channel

```cpp
channel<int> ch(0);  // Unbuffered (synchronous)

// send() blocks until recv() is called
// recv() blocks until send() is called
```

### Select Pattern (Manual)

```cpp
task<void> select_example(channel<int>& ch1, channel<int>& ch2) {
    // Try ch1 first
    if (auto val = ch1.try_recv()) {
        std::println("ch1: {}", *val);
        co_return;
    }
    
    // Try ch2
    if (auto val = ch2.try_recv()) {
        std::println("ch2: {}", *val);
        co_return;
    }
    
    // Both empty, wait for first available
    // (requires custom select implementation)
}
```

### Use Cases

**Producer-Consumer**:
```cpp
task<void> pipeline(io_context& ctx) {
    channel<std::string> ch(100);
    
    // Producer
    spawn(ctx, [&]() -> task<void> {
        for (int i = 0; i < 1000; ++i) {
            co_await ch.send(std::format("item-{}", i));
        }
        ch.close();
    }());
    
    // Consumer
    spawn(ctx, [&]() -> task<void> {
        while (auto item = co_await ch.recv()) {
            co_await process(*item);
        }
    }());
}
```

**Fan-Out**:
```cpp
task<void> fan_out(io_context& ctx) {
    channel<int> input(10);
    channel<int> output1(10);
    channel<int> output2(10);
    
    // Distributor
    spawn(ctx, [&]() -> task<void> {
        while (auto val = co_await input.recv()) {
            co_await output1.send(*val);
            co_await output2.send(*val);
        }
        output1.close();
        output2.close();
    }());
}
```

**Fan-In**:
```cpp
task<void> fan_in(io_context& ctx) {
    channel<int> input1(10);
    channel<int> input2(10);
    channel<int> output(10);
    
    // Merger
    spawn(ctx, [&]() -> task<void> {
        while (true) {
            if (auto val = co_await input1.recv()) {
                co_await output.send(*val);
            } else if (auto val = co_await input2.recv()) {
                co_await output.send(*val);
            } else {
                break;  // Both closed
            }
        }
        output.close();
    }());
}
```

## Wait Group

Barrier for waiting on multiple tasks.

### Basic Usage

```cpp
wait_group wg;

task<void> parallel_work(io_context& ctx) {
    wg.add(3);  // Expect 3 tasks
    
    spawn(ctx, [&]() -> task<void> {
        co_await task1();
        wg.done();
    }());
    
    spawn(ctx, [&]() -> task<void> {
        co_await task2();
        wg.done();
    }());
    
    spawn(ctx, [&]() -> task<void> {
        co_await task3();
        wg.done();
    }());
    
    co_await wg.wait();  // Wait for all 3 tasks
    std::println("All tasks complete");
}
```

### Dynamic Task Count

```cpp
task<void> dynamic_tasks(io_context& ctx) {
    wait_group wg;
    
    for (int i = 0; i < 10; ++i) {
        wg.add(1);
        spawn(ctx, [&, i]() -> task<void> {
            co_await process_item(i);
            wg.done();
        }());
    }
    
    co_await wg.wait();
}
```

### Use Cases

**Parallel Requests**:
```cpp
task<std::vector<response>> fetch_all(
    io_context& ctx,
    const std::vector<std::string>& urls
) {
    std::vector<response> results(urls.size());
    wait_group wg;
    
    for (size_t i = 0; i < urls.size(); ++i) {
        wg.add(1);
        spawn(ctx, [&, i]() -> task<void> {
            results[i] = co_await http_get(urls[i]);
            wg.done();
        }());
    }
    
    co_await wg.wait();
    co_return results;
}
```

## Cancel Token

Cooperative cancellation mechanism.

### Basic Usage

```cpp
cancel_token token;

task<void> cancellable_work(cancel_token token) {
    while (!token.is_cancelled()) {
        co_await do_work();
    }
    std::println("Work cancelled");
}

// Start work
spawn(ctx, cancellable_work(token));

// Later: cancel
token.cancel();
```

### Cancellation Callback

```cpp
task<void> with_cleanup(cancel_token token) {
    token.on_cancel([] {
        std::println("Cleaning up...");
    });
    
    while (!token.is_cancelled()) {
        co_await do_work();
    }
}
```

### Timeout with Cancellation

```cpp
task<void> timeout_example(io_context& ctx) {
    cancel_token token;
    
    // Start work
    spawn(ctx, long_running_task(token));
    
    // Cancel after timeout
    co_await async_sleep(ctx, 5s);
    token.cancel();
}
```

### Hierarchical Cancellation

```cpp
task<void> parent_task(cancel_token parent_token) {
    cancel_token child_token;
    
    // Cancel child when parent cancelled
    parent_token.on_cancel([&] {
        child_token.cancel();
    });
    
    spawn(ctx, child_task(child_token));
}
```

## Performance Comparison

| Primitive | Acquire Cost | Release Cost | Memory |
|-----------|--------------|--------------|--------|
| `mutex` | ~50 ns | ~30 ns | 64 bytes |
| `shared_mutex` | ~60 ns (read) | ~40 ns | 128 bytes |
| `semaphore` | ~50 ns | ~30 ns | 64 bytes |
| `channel<T>` | ~100 ns | ~100 ns | 128 + buffer |
| `wait_group` | ~20 ns (add) | ~30 ns (done) | 32 bytes |

## Best Practices

### 1. Use RAII Guards

```cpp
// GOOD: Automatic unlock
auto guard = co_await mtx.scoped_lock();

// BAD: Manual unlock (error-prone)
co_await mtx.lock();
// ... exception might skip unlock ...
mtx.unlock();
```

### 2. Minimize Critical Sections

```cpp
// GOOD: Short critical section
{
    auto guard = co_await mtx.scoped_lock();
    ++counter;
}
co_await expensive_operation();  // Outside lock

// BAD: Long critical section
auto guard = co_await mtx.scoped_lock();
++counter;
co_await expensive_operation();  // Holds lock!
```

### 3. Avoid Deadlocks

```cpp
// BAD: Potential deadlock
task<void> task1() {
    co_await mtx1.lock();
    co_await mtx2.lock();  // Deadlock if task2 holds mtx2
}

task<void> task2() {
    co_await mtx2.lock();
    co_await mtx1.lock();  // Deadlock if task1 holds mtx1
}

// GOOD: Consistent lock ordering
task<void> task1() {
    co_await mtx1.lock();
    co_await mtx2.lock();
}

task<void> task2() {
    co_await mtx1.lock();  // Same order
    co_await mtx2.lock();
}
```

### 4. Use Channels for Communication

```cpp
// GOOD: Channel-based communication
channel<message> ch(10);
spawn(ctx, producer(ch));
spawn(ctx, consumer(ch));

// BAD: Shared state with mutex
mutex mtx;
std::queue<message> queue;
// More complex, error-prone
```

### 5. Prefer `wait_group` Over Manual Counting

```cpp
// GOOD: wait_group
wait_group wg;
wg.add(N);
// ... spawn tasks ...
co_await wg.wait();

// BAD: Manual counting
std::atomic<int> counter{N};
// ... spawn tasks that decrement ...
while (counter > 0) {
    co_await async_sleep(ctx, 10ms);  // Polling!
}
```

## Next Steps

- **[Coroutines](coroutines.md)** - Understand `task<T>` and async/await
- **[I/O Context](io-context.md)** - Event loop and scheduling
- **[TCP/UDP Guide](../protocols/tcp-udp.md)** - Network programming
- **[HTTP Server](../protocols/http.md)** - Build web services
