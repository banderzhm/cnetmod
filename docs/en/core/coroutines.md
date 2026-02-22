# Coroutines

This guide covers cnetmod's coroutine system, including `task<T>`, `spawn()`, and async/await patterns.

## Overview

cnetmod uses C++20 coroutines for asynchronous programming. Coroutines provide:

- **Zero overhead**: Compiled to state machines, no callback allocations
- **Natural syntax**: `co_await` instead of callback hell
- **Composability**: Easy to combine async operations
- **Cancellation**: Built-in support for cooperative cancellation

## The `task<T>` Type

`task<T>` is the fundamental coroutine type in cnetmod. It represents a lazy, asynchronous computation that produces a value of type `T`.

### Basic Usage

```cpp
import cnetmod;
using namespace cnetmod;

// Define an async function
task<int> compute_value() {
    co_await async_sleep(ctx, 1s);
    co_return 42;
}

// Call it
task<void> main_task(io_context& ctx) {
    int result = co_await compute_value();
    std::println("Result: {}", result);
}
```

### Key Characteristics

1. **Lazy evaluation**: `task<T>` doesn't start executing until awaited
2. **Single-shot**: Can only be awaited once
3. **Move-only**: Cannot be copied
4. **Exception propagation**: Exceptions propagate through `co_await`

```cpp
task<int> may_throw() {
    if (error_condition)
        throw std::runtime_error("Error!");
    co_return 42;
}

task<void> caller() {
    try {
        int value = co_await may_throw();
    } catch (const std::exception& e) {
        std::println("Caught: {}", e.what());
    }
}
```

## The `spawn()` Function

`spawn()` starts a coroutine without waiting for its result (fire-and-forget).

### Basic Usage

```cpp
task<void> background_work() {
    co_await async_sleep(ctx, 5s);
    std::println("Background work done");
}

int main() {
    io_context ctx;
    
    // Start background task
    spawn(ctx, background_work());
    
    // Continue immediately
    ctx.run();
}
```

### Use Cases

- **Concurrent connections**: Handle multiple clients simultaneously
- **Background tasks**: Periodic cleanup, monitoring
- **Event handlers**: Fire-and-forget event processing

```cpp
task<void> handle_client(tcp_socket client) {
    // Handle client connection
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
        // Spawn concurrent handler
        spawn(ctx, handle_client(std::move(client)));
    }
}
```

## Coroutine Keywords

### `co_await`

Suspends the coroutine until the awaited operation completes.

```cpp
// Wait for I/O
int n = co_await socket.async_recv(buffer, size);

// Wait for timer
co_await async_sleep(ctx, 2s);

// Wait for another task
auto result = co_await compute_value();
```

### `co_return`

Returns a value from a coroutine.

```cpp
task<int> get_value() {
    co_return 42;
}

task<void> no_value() {
    // Do work
    co_return;  // or just fall off the end
}
```

### `co_yield`

Not used in cnetmod (we use `channel<T>` for generators).

## Symmetric Transfer

cnetmod uses symmetric transfer for tail-call optimization, preventing stack growth.

```cpp
task<int> recursive_task(int n) {
    if (n == 0)
        co_return 0;
    
    // This doesn't grow the stack!
    int result = co_await recursive_task(n - 1);
    co_return result + 1;
}
```

**How it works**: Instead of resuming the caller's coroutine frame, we directly transfer control to the next coroutine, avoiding stack buildup.

## Coroutine Lifetime

### Automatic Cleanup

Coroutine frames are automatically destroyed when:
- The coroutine completes normally
- An exception propagates out
- The coroutine is cancelled

```cpp
task<void> example() {
    tcp_socket socket(ctx);  // RAII resource
    co_await socket.async_connect(addr);
    // socket automatically closed when coroutine ends
}
```

### Detached Tasks

`spawn()` creates a detached task that manages its own lifetime:

```cpp
spawn(ctx, []() -> task<void> {
    co_await async_sleep(ctx, 1s);
    std::println("Done");
}());
// Coroutine continues even if spawn() caller exits
```

## Cancellation

Use `cancel_token` for cooperative cancellation:

```cpp
task<void> cancellable_operation(cancel_token token) {
    while (!token.is_cancelled()) {
        co_await do_work();
    }
    std::println("Operation cancelled");
}

// Usage
cancel_token token;
spawn(ctx, cancellable_operation(token));

// Later: cancel the operation
token.cancel();
```

## Common Patterns

### Sequential Operations

```cpp
task<void> sequential() {
    auto result1 = co_await operation1();
    auto result2 = co_await operation2(result1);
    auto result3 = co_await operation3(result2);
    co_return result3;
}
```

### Concurrent Operations

```cpp
task<void> concurrent(io_context& ctx) {
    // Start both operations
    spawn(ctx, operation1());
    spawn(ctx, operation2());
    
    // Or use wait_group for synchronization
    wait_group wg;
    wg.add(2);
    
    spawn(ctx, [&]() -> task<void> {
        co_await operation1();
        wg.done();
    }());
    
    spawn(ctx, [&]() -> task<void> {
        co_await operation2();
        wg.done();
    }());
    
    co_await wg.wait();
}
```

### Timeout Pattern

```cpp
task<void> with_timeout_example(io_context& ctx) {
    try {
        auto result = co_await with_timeout(ctx, slow_operation(), 5s);
        std::println("Success: {}", result);
    } catch (const timeout_error&) {
        std::println("Operation timed out");
    }
}
```

### Retry Pattern

```cpp
task<int> retry_operation(io_context& ctx) {
    retry_policy policy{
        .max_attempts = 3,
        .initial_delay = 100ms,
        .max_delay = 5s,
        .backoff_multiplier = 2.0
    };
    
    co_return co_await retry(ctx, policy, []() -> task<int> {
        co_return co_await unreliable_operation();
    });
}
```

## Performance Considerations

### Coroutine Frame Size

- Typical `task<T>` frame: ~64 bytes
- Includes: promise object, local variables, suspend points
- Allocated on heap (once per coroutine)

### Suspend/Resume Cost

- Symmetric transfer: 2-3 CPU cycles
- No function call overhead
- No stack manipulation

### Memory Allocation

```cpp
// Single allocation per coroutine
task<void> example() {
    int local = 42;  // Part of coroutine frame
    co_await operation();
}
```

To avoid allocations, use:
- Small coroutine frames (few local variables)
- Move semantics for large objects
- `span<>` and `string_view` instead of copies

## Debugging Coroutines

### Common Issues

**1. Dangling References**

```cpp
// BAD: reference to temporary
task<void> bad_example() {
    std::string temp = "hello";
    co_await operation();
    // temp might be destroyed if coroutine is moved
}

// GOOD: value or move
task<void> good_example() {
    std::string value = "hello";  // Stored in coroutine frame
    co_await operation();
}
```

**2. Forgetting to `co_await`**

```cpp
// BAD: task not awaited
task<void> bad() {
    compute_value();  // Does nothing!
}

// GOOD: await the task
task<void> good() {
    co_await compute_value();
}
```

**3. Awaiting Moved Task**

```cpp
// BAD: task moved
task<void> bad() {
    auto t = compute_value();
    auto t2 = std::move(t);
    co_await t;  // Undefined behavior!
}
```

### Debugging Tips

1. **Use sanitizers**: AddressSanitizer catches use-after-free
2. **Enable logging**: Add debug prints at suspend/resume points
3. **Check lifetimes**: Ensure objects outlive coroutines
4. **Use RAII**: Let destructors handle cleanup

## Integration with Blocking Code

### Call Async from Sync

```cpp
int sync_function() {
    io_context ctx;
    return blocking_invoke(ctx, async_operation());
}
```

### Call Sync from Async

```cpp
task<int> async_function(io_context& ctx) {
    // Offload blocking work to thread pool
    co_return co_await blocking_invoke(ctx, [] {
        return expensive_computation();
    });
}
```

## Best Practices

1. **Always `co_await` tasks**: Don't discard task results
2. **Use `spawn()` for fire-and-forget**: Clear intent
3. **Prefer RAII**: Let destructors manage resources
4. **Avoid captures by reference**: Use values or move
5. **Use `cancel_token`**: For graceful shutdown
6. **Keep frames small**: Minimize local variables
7. **Use `span<>` and `string_view`**: Avoid copies

## Next Steps

- **[I/O Context](io-context.md)** - Event loop and scheduling
- **[Synchronization Primitives](sync-primitives.md)** - Mutex, channel, semaphore
- **[TCP/UDP Guide](../protocols/tcp-udp.md)** - Network programming
- **[HTTP Server](../protocols/http.md)** - Build web services
