# `task<T>` Associated Executor Proposal

## Status

Design proposal only. The current `task<T>` promise and resumption behavior are
unchanged.

## Problem statement

`task<T>` stores only its awaiting continuation. A coroutine therefore resumes
where the awaited operation completes unless that operation explicitly posts
back to an `io_context`. This is intentional for cnetmod I/O awaitables and for
`blocking_invoke()`/`application_runtime::offload()`, but third-party awaitables
can resume a caller on an arbitrary completion thread.

Application code previously compensated by passing `io_context&` through many
layers. The Application runtime facade solves that composition problem without
changing every task's ABI or scheduling semantics.

## Compatibility risks of binding every task

1. Adding executor state changes every promise object and coroutine-frame size.
2. Symmetric transfer in `final_suspend()` would become a scheduled transfer,
   changing ordering, latency, allocation behavior, and exception boundaries.
3. Existing I/O awaitables already resume through their owning context. An
   unconditional associated executor adds redundant posts and can reorder work.
4. Detached tasks, task groups, `when_all`, generators, and third-party bridge
   adapters require independent lifetime and cancellation audits.
5. A pointer to an executor is unsafe unless the executor lifetime dominates
   every suspended coroutine, including shutdown and failed startup paths.

## Recommended experiment

Introduce an opt-in wrapper rather than changing `task<T>`:

```cpp
template <typename T>
auto resume_on(io_context& target, task<T> operation) -> task<T>;
```

The wrapper should preserve symmetric transfer inside the operation and post
only the final continuation to `target`. A later type-erased `scheduler_ref`
may support pools and third-party schedulers after measuring frame size,
cross-thread post count, cancellation latency, and shutdown races.

## Acceptance gates

- No ABI or semantic change to existing `task<T>`.
- Explicit executor lifetime contract and use-after-free tests.
- Windows IOCP, Linux io_uring/epoll, and macOS kqueue coverage.
- Tests for inline completion, allocation failure, cancellation, nested tasks,
  task groups, `when_all`, detached ownership, and shutdown during resumption.
- Benchmarks showing the number and cost of additional scheduling hops.

Until those gates pass, Application code should use `application_runtime`,
`starts_on()`, `spawn_on()`, or `blocking_invoke()` as appropriate.
