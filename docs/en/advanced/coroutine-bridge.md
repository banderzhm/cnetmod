# Coroutine Bridge Module (`cnetmod.coro.bridge`)

## Purpose

`cnetmod.coro.bridge` integrates blocking APIs and foreign C++ awaitables with
the cnetmod coroutine runtime without exposing third-party executor types in a
module interface.

```cpp
import cnetmod.coro.bridge;
```

The public API uses cnetmod-owned types only. The executor implementation is
kept in regular `.cpp` files, so applications do not import or depend on
stdexec implementation details.

## API Overview

| API | Purpose |
|---|---|
| `blocking_invoke(pool, io, fn)` | Run a blocking callable on a worker pool and resume on an `io_context` |
| `from_awaitable<T>(awaitable)` | Adapt a foreign C++ awaitable to `task<T>` |

## Blocking Calls

Use `blocking_invoke` for a library that provides only a synchronous API. The
callable runs on `thread_pool`; after it completes, the coroutine resumes on
the supplied `io_context`.

```cpp
auto load_user(server_context& server, io_context& io) -> task<user>
{
    co_return co_await blocking_invoke(server.pool(), io, []
        {
            return legacy_database.load_user(42);
        });
}
```

Both value-returning and `void` callables are supported. Independent blocking
operations can be composed with `when_all`:

```cpp
auto [message, rows] = co_await when_all(
    blocking_invoke(pool, io, [] { return queue.consume(); }),
    blocking_invoke(pool, io, [] { return database.query(); }));
```

Do not access an `io_context`-owned object from inside the blocking callable.
That callable runs on a pool thread. Continue event-loop work only after
`blocking_invoke` has resumed the coroutine on `io`.

## Foreign Awaitables

`from_awaitable<T>` converts any compatible C++ awaitable into a cnetmod
`task<T>`:

```cpp
auto value = co_await from_awaitable<int>(third_party_async_call());
co_await from_awaitable<void>(third_party_async_flush());
```

Most foreign awaitables can also be awaited directly. The adapter is useful
when an API requires a concrete `task<T>` or when composing heterogeneous
awaitables.

## Native Scheduling

`io_scheduler` is a coroutine scheduler facade, not a sender. Awaiting
`schedule()` posts the current coroutine to the selected `io_context`:

```cpp
import cnetmod.executor.scheduler;

auto continue_on(io_context& io) -> task<void>
{
    io_scheduler scheduler{io};
    co_await scheduler.schedule();
    // Running through io's post queue here.
}
```

For CPU work, use `pool_post_awaitable` or `blocking_invoke`; for a synchronous
program entry point, use the task runtime's native `sync_wait`.

## Example

See `examples/concurrency/blocking_bridge_demo.cpp` for blocking-call,
concurrent offload, native task composition, and foreign-awaitable examples.

Key guarantees:

- no stdexec type is exported through a cnetmod module BMI;
- blocking work does not run on the event-loop thread;
- `blocking_invoke` resumes on the requested `io_context`;
- callable and awaitable exceptions propagate through `task<T>`.
