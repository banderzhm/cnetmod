/// cnetmod.coro — 协程模块聚合
/// 一次导入即可获得所有协程原语：
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
