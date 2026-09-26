# Application multi-event-loop architecture

## Status

Implemented. `application.io_threads` controls the number of business HTTP
event loops and defaults to `1`. Values outside `[1, 1024]` fail configuration
validation. `CNETMOD_IO_THREADS` supplies the environment-layer value.

When the value is greater than one, the host owns:

- one control loop for lifecycle, management endpoints and health probes;
- `application.io_threads` business loops used by accepted HTTP connections;
- one CPU pool, independent from the I/O loops.

`server_context` borrows that Application CPU pool instead of allocating a
second executor. `application.cpu_threads` is therefore the single process-wide
CPU-offload budget in both single-loop and multi-loop mode.

Every accepted connection remains on its assigned business loop for its whole
lifetime, including request handlers, response writes and SSE chunks.

## Affinity contract

Every auto-configured integration declares one `integration_loop_mode`:

| Mode | Contract |
|---|---|
| `loop_local` | One client or pool shard per control/business loop. Calls select the shard from `io_context::current()` and never steal a connection from another loop. |
| `owned` | One instance stays on its owner loop. Calls, results and streaming callbacks are explicitly marshalled between owner and caller loops. |
| `shared` | The implementation is intrinsically safe for concurrent access from all loops. |
| `single_loop_only` | Building with `application.io_threads > 1` fails during registration. There is no unsafe fallback. |

The built-in HTTP client, MySQL, PostgreSQL and Redis integrations are
`loop_local`. Redis Cluster also creates one slot-aware client per loop. OpenAI
model clients are `owned`; model invocations run on the owner loop and stream
handlers return to the request loop before touching HTTP/SSE state.

Pool `minimum_size` and `maximum_size` remain aggregate application limits.
They are divided by quotient and remainder across shards. A maximum smaller
than the number of application loops is rejected because it cannot provide one
usable shard per loop.

## Coroutine guarantees

- `io_context::current()` identifies the loop dispatching the current thread.
- `execution_context::event_loop()` returns that caller loop and falls back to
  the control loop only when invoked outside every event loop;
  `control_event_loop()` is the explicit lifecycle/owner accessor.
- `resume_on(loop, task)` publishes either the value or exception on `loop`.
- `application_runtime::offload()` captures the caller loop before entering the
  CPU pool and returns to that exact loop.
- The async mutex, shared mutex, semaphore, wait group and channel remember the
  waiting coroutine's loop. A foreign-thread release posts the continuation to
  that loop instead of resuming it inline on the releasing thread.
- `async_file_template` and `rest_template` select caller-local resources.
- `schedule_on_cpu()` users must capture their loop first and call
  `resume_to_event_loop(captured_loop)`; `offload()` is preferred.

These rules are required for correctness, not only throughput. Resuming a
request coroutine on another loop can otherwise submit socket, database or SSE
operations through the wrong IOCP/io_uring/epoll owner.

## Components

`component_collection` supports two explicit scopes:

- `singleton<T>()` constructs one immutable or internally synchronized object;
- `event_loop<T>()` eagerly constructs one object for every control/business
  loop and resolves it through `io_context::current()`.
- `event_loop_alias<Interface, Implementation>()` preserves that scope when a
  loop-local implementation is exposed through an interface.

A singleton factory cannot implicitly consume an event-loop component because
there is no unambiguous owner. Resolution fails during the build instead of
choosing a random shard.

## Shutdown

The control loop coordinates shutdown. Connection cancellation is thread-safe,
but worker-owned sockets close on their own loop. Sharded maintenance tasks are
joined through loop-aware wait groups. Services drain outstanding leases before
the host stops and joins every business loop.

## Verification requirements

Changes to this architecture must keep these tests green:

- cross-loop coroutine primitive resumption;
- event-loop component identity;
- HTTP connection distribution across at least two business loops;
- CPU offload returning to the originating request loop;
- strict rejection of unknown pool loops and exact aggregate pool capacity;
- real MySQL/Redis lifecycle, cancellation and degraded-dependency tests;
- streaming model callbacks touching response state only on the request loop.
