# Application multi-event-loop design

## Status

Design only. The Application host runs one event loop; `application.event_loops`
is intentionally not a configuration key yet. This document records why a
correct implementation needs loop affinity across every integration, and the
staged plan that keeps each step verifiable. It builds on
`task-associated-executor-proposal.md` (opt-in `resume_on`, no `task<T>` ABI change).

## Why a flag is not enough

The HTTP server already distributes connections across worker loops through
`server_context`. The Application cannot simply enable it:

| Shared resource | Owning loop today | Hazard when a handler runs on another loop |
|---|---|---|
| Redis, MySQL, PostgreSQL, MongoDB pools | primary | pool state, waiter queues and leases are loop-affine |
| `chat_model_pool` and provider clients | primary | same, plus stream callbacks resume on the owner loop |
| `async_file_template`, `rest_template`, `json_template` | primary | completions resume on the primary loop |
| `application_runtime::offload()` / `resume_to_event_loop()` | primary | returns the handler to the wrong loop |
| `request_context` socket I/O, SSE writes | connection loop | writing from the primary loop submits to another ring/epoll |

Two tempting shortcuts are both unsafe:

1. **Handlers on worker loops, services unchanged.** Every service call races
   with the primary loop's pool state.
2. **I/O on worker loops, handlers hopped to the primary loop.** Plain responses
   work, but SSE and streaming bodies write to a socket registered with the
   worker loop from the primary thread.

## Target model

- Each business event loop owns its connections. Handlers always run on the
  connection's loop.
- Every managed integration declares its affinity:
  - **sharded**: one pool per loop (Redis, SQL pools, HTTP clients). Calls
    stay on the caller's loop with no hop. Capacity settings become per-loop.
  - **owned**: a single instance on its owner loop (chat model pool, stateful
    protocol sessions). Calls hop with `resume_on(owner)` and return with
    `resume_on(caller)`; streaming callbacks are marshalled back to the caller
    loop before they touch `request_context`.
- `application_runtime` resolves "current loop" through an explicit accessor
  (`io_context::current()`), and `offload()` resumes on the caller's loop.
- Health, supervision and management endpoints stay on the primary loop.

## Staged plan and acceptance gates

1. **Primitives.** Public `io_context::current()`; `resume_on(io_context&, task<T>)`
   per the executor proposal. Gates: inline completion, cancellation during a hop,
   shutdown during resumption, allocation failure; IOCP, io_uring, epoll, kqueue.
2. **Caller-loop runtime.** `offload()`, `resume_to_event_loop()` and the file,
   REST and JSON templates resume on the caller's loop. Gate: no behavior change
   with one loop (existing suite green).
3. **Sharded pools.** Per-loop pool instances behind the existing service
   facades; health aggregates per-loop probes. Gates: per-loop capacity limits,
   recovery of one loop's pool while others serve, graceful drain per loop.
4. **Owned integrations.** Hop wrappers for chat models and stateful sessions,
   including stream marshalling. Gates: SSE under concurrent hops, cancellation
   of a stream mid-hop, no chunk reordering.
5. **Host.** `application.event_loops` (default 1) creates worker loops, starts
   sharded services per loop, and joins loops in reverse order at shutdown.
   Gates: TSAN clean on Linux; soak test with SSE, repositories and chat models;
   benchmarks of hop count and tail latency against the single-loop baseline.

Until stage 5 ships, scale CPU-bound work with `offload()` and scale I/O by
running more processes behind a load balancer.
