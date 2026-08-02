/// cnetmod.coro — Coroutine module aggregation
/// Import once to get all coroutine primitives:
///   task / spawn / timer / cancel / awaitable / bridge /
///   channel / mutex / shared_mutex / semaphore / wait_group

export module cnetmod.coro;

export import cnetmod.coro.task;
export import cnetmod.coro.spawn;
export import cnetmod.coro.timer;
export import cnetmod.coro.cancel;
export import cnetmod.coro.awaitable;
export import cnetmod.coro.bridge;
export import cnetmod.coro.channel;
export import cnetmod.coro.mutex;
export import cnetmod.coro.shared_mutex;
export import cnetmod.coro.semaphore;
export import cnetmod.coro.wait_group;
export import cnetmod.coro.retry;
export import cnetmod.coro.circuit_breaker;
