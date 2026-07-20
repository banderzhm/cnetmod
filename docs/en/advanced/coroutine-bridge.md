# Coroutine Bridge Module (cnetmod.coro.bridge)

## Background

In coroutine-based async servers, you often need to integrate third-party libraries that only provide **blocking/multi-threaded APIs** (such as RabbitMQ C client, gRPC synchronous stubs, legacy database drivers), or interoperate with the **stdexec sender/receiver** ecosystem. If you directly call these blocking APIs in a `task<T>` coroutine, it will block the `io_context` event loop thread, causing the entire server to stop responding.

`bridge.cppm` provides three bridging tools to completely solve these cross-world interoperability problems.

## Module Import

```cpp
import cnetmod.coro.bridge;
```

This module depends on: `cnetmod.coro.task`, `cnetmod.io.io_context`, `cnetmod.executor.pool`.

---

## Tools Overview

| Tool | Signature | Purpose |
|------|-----------|---------|
| `blocking_invoke` | `(pool, io, fn) → task<R>` | Offload blocking calls to thread pool |
| `await_sender<T>` | `(sender) → awaitable` | co_await any stdexec sender |
| `from_awaitable<T>` | `(awaitable) → task<T>` | Wrap third-party coroutine awaitable |

---

## 1. blocking_invoke — Blocking API Bridge

### Principle

```
io_context thread        thread pool thread      io_context thread
      │                                         
      ├─ co_await ─────► pool_post_awaitable     
      │                     │                    
      │                     ├─ fn()  (blocking execution)   
      │                     │                    
      │  post_awaitable ◄───┤                    
      │                                          
      ├─ co_return result ◄──────────────────────┘
```

The coroutine suspends and switches to the stdexec thread pool to execute the blocking operation. After completion, it automatically switches back to the `io_context` event loop thread via `post_awaitable`. The entire process is transparent to the caller.

### Function Signature

```cpp
// Version with return value
template <typename F>
    requires std::invocable<std::decay_t<F>>
          && (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>)
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn)
    -> task<std::invoke_result_t<std::decay_t<F>>>;

// void return value version
template <typename F>
    requires std::invocable<std::decay_t<F>>
          && std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn)
    -> task<void>;
```

### Usage

```cpp
auto handle_message(server_context& ctx) -> task<void> {
    auto& pool = ctx.pool();
    auto& io   = ctx.accept_io();

    // RabbitMQ blocking consume → executed in thread pool, doesn't block event loop
    auto msg = co_await blocking_invoke(pool, io, [&] {
        return rabbitmq_client.basic_consume("orders", 5000);
    });

    // gRPC synchronous call
    auto reply = co_await blocking_invoke(pool, io, [&] {
        return grpc_stub.SayHello(request);
    });

    // void return value also supported
    co_await blocking_invoke(pool, io, [&] {
        rabbitmq_client.basic_publish("exchange", "routing_key", payload);
    });
}
```

### Concurrent Execution of Multiple Blocking Operations

Combined with `when_all`, you can execute multiple blocking operations concurrently. Total time equals the slowest one, not sequential accumulation:

```cpp
// Total time ≈ max(200ms, 100ms) = 200ms, not 300ms
auto [msg, rows] = co_await when_all(
    blocking_invoke(pool, io, [] {
        return rabbitmq.consume("events", 200);  // 200ms
    }),
    blocking_invoke(pool, io, [] {
        return db.query("SELECT * FROM orders");  // 100ms
    })
);
```

---

## 2. await_sender — co_await stdexec sender

### Principle

`sender_awaitable<T, Sender>` is an awaitable adapter with internal implementation:

1. `await_suspend`: Calls `stdexec::connect(sender, bridge_receiver)` to construct `op_state`, then `start`
2. When sender completes: `bridge_receiver` stores the result and `resume()` the coroutine
3. `await_resume`: Returns the stored result (or rethrows exception)

`op_state` uses placement storage in the awaitable object (awaitable is embedded in coroutine frame, lifetime-safe during suspension).

### Function Signature

```cpp
// Factory function — T must be explicitly specified
template <typename T, typename Sender>
auto await_sender(Sender&& sndr) -> sender_awaitable<T, std::decay_t<Sender>>;
```

### Usage

```cpp
// co_await a task_sender<int> (stdexec sender converted from task<T>)
auto val = co_await await_sender<int>(as_sender(compute(42)));

// co_await a void sender
co_await await_sender<void>(as_sender(fire_and_forget()));

// Can also co_await any other object conforming to stdexec sender concept
// For example, sender produced by io_scheduler::schedule()
auto sched = io_scheduler{io_ctx};
co_await await_sender<void>(sched.schedule());
```

### sender_awaitable Low-Level Type

If you need finer control, you can directly use the `sender_awaitable<T, Sender>` type:

```cpp
auto sndr = /* some stdexec sender */;
auto aw = sender_awaitable<int, decltype(sndr)>{std::move(sndr)};
auto result = co_await std::move(aw);
```

Generally, it's recommended to use the `await_sender<T>(sndr)` factory function, which automatically deduces the Sender type.

---

## 3. from_awaitable — Wrap Third-Party Awaitable

### Principle

Wraps any type that implements the `await_ready / await_suspend / await_resume` interface (or implements `operator co_await()`) into a cnetmod `task<T>`.

Implementation is extremely concise:

```cpp
template <typename T, typename Awaitable>
auto from_awaitable(Awaitable&& aw) -> task<T> {
    if constexpr (std::is_void_v<T>) {
        co_await std::forward<Awaitable>(aw);
    } else {
        co_return co_await std::forward<Awaitable>(aw);
    }
}
```

### Usage

```cpp
// Assume third-party library returns its own awaitable type
auto result = co_await from_awaitable<int>(third_party_async_call());

// void type
co_await from_awaitable<void>(third_party_fire_and_forget());
```

> **Note**: cnetmod's `task<T>::promise_type` naturally supports `co_await` on any awaitable (via `await_transform` passthrough), so in most cases you can directly `co_await third_party_awaitable{}` without explicitly using `from_awaitable`. `from_awaitable` is mainly used for **type erasure** scenarios — unifying different types of awaitables into `task<T>` for storage, passing, or use with combinators like `when_all`.

---

## Complete Example

See `examples/blocking_bridge_demo.cpp`, which demonstrates four scenarios:

1. **blocking_invoke** — Simulating RabbitMQ consume/publish, database queries
2. **when_all + blocking_invoke** — Concurrent execution of multiple blocking operations
3. **await_sender** — co_await stdexec `task_sender`
4. **from_awaitable** — Wrapping external awaitable types

How to run:

```bash
cmake --build build --target example_blocking_bridge_demo
./build/examples/Debug/example_blocking_bridge_demo    # Windows
./build/examples/example_blocking_bridge_demo           # Linux/macOS
```

---

## Typical Application Architecture

```
┌─────────────────────────────────────────────────────┐
│                 Coroutine task<T> World              │
│                                                     │
│  HTTP handler ──► blocking_invoke ──► RabbitMQ (blocking) │
│       │                                              │
│       ├────────► blocking_invoke ──► gRPC sync (blocking) │
│       │                                              │
│       ├────────► await_sender ───► stdexec pipeline   │
│       │                                              │
│       └────────► from_awaitable ► third-party coro task │
│                                                     │
├────────────── io_context (event loop thread) ────────┤
├────────────── exec::static_thread_pool ──────────────┤
└─────────────────────────────────────────────────────┘
```

**Key Guarantees**:

- After `blocking_invoke` completes, the coroutine always returns to the `io_context` thread, safe to continue async I/O operations
- `sender_awaitable`'s `op_state` lifetime is bound to the coroutine frame, won't dangle
- All bridging tools correctly propagate exceptions (`std::exception_ptr`)
