# Executor 与 Bridge

> cnetmod 原生协程调度、线程池、多核服务器上下文，以及阻塞 API 和第三方
> awaitable 的桥接方式。

**import**: `import cnetmod.executor.scheduler;` / `import cnetmod.executor.pool;` /
`import cnetmod.coro.bridge;`

## 核心规则

- 公开模块接口只暴露 cnetmod 自有类型，不导出 stdexec/exec 类型。
- stdexec 仅允许作为普通 `.cpp` 中的内部实现细节，不得出现在 `.cppm`。
- I/O 协程使用 `io_scheduler::schedule()` 或 `post_awaitable` 切换执行域。
- CPU 密集或阻塞调用使用 `thread_pool`、`pool_post_awaitable`、
  `blocking_invoke`，禁止阻塞 `io_context`。
- 同步入口等待 `task<T>` 使用原生 `sync_wait`。

## `io_scheduler`

`io_scheduler` 是绑定 `io_context` 的原生协程调度门面。

```cpp
export class io_scheduler
{
public:
    explicit io_scheduler(io_context& context) noexcept;
    auto operator==(const io_scheduler& other) const noexcept -> bool;
    [[nodiscard]] auto context() const noexcept -> io_context&;
    [[nodiscard]] auto schedule() const noexcept -> schedule_awaitable;
};
```

```cpp
auto run_on(io_context& io) -> task<void>
{
    io_scheduler scheduler{io};
    co_await scheduler.schedule();
    co_return;
}
```

## `thread_pool` 与 `pool_post_awaitable`

`thread_pool` 是 cnetmod 自有 PImpl 门面，具体执行器类型只存在于 `.cpp`。

```cpp
export class thread_pool
{
public:
    explicit thread_pool(
        unsigned thread_count = std::thread::hardware_concurrency());
    ~thread_pool();
    void request_stop() noexcept;
};

export struct pool_post_awaitable
{
    thread_pool& pool;
    explicit pool_post_awaitable(thread_pool& value) noexcept;
    auto await_ready() const noexcept -> bool;
    void await_suspend(std::coroutine_handle<> coroutine) noexcept;
    void await_resume() noexcept;
};
```

`co_await pool_post_awaitable{pool}` 后，协程在线程池线程恢复。需要继续 I/O
时必须再切回目标 `io_context`。

## `server_context`

`server_context` 管理 accept `io_context`、多个 worker `io_context` 和 CPU
线程池。

```cpp
export class server_context
{
public:
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency(),
        thread_affinity_options affinity = {});

    [[nodiscard]] auto accept_io() noexcept -> io_context&;
    [[nodiscard]] auto next_worker_io() noexcept -> io_context&;
    [[nodiscard]] auto worker_count() const noexcept -> unsigned;
    [[nodiscard]] auto worker_ios() -> std::vector<io_context*>;
    [[nodiscard]] auto pool() noexcept -> thread_pool&;

    template <typename F>
    requires std::invocable<std::decay_t<F>>
    auto offload(io_context& return_to, F&& fn);

    void spawn_next(task<void> value);
    void run();
    void stop();
};
```

## `blocking_invoke`

阻塞函数在线程池执行，完成后自动回到指定 `io_context`。

```cpp
auto value = co_await blocking_invoke(server.pool(), io, []
    {
        return blocking_client.request();
    });
```

互不依赖的调用可通过 `when_all` 并发卸载。不要在阻塞函数体中访问由事件
循环线程独占的对象。

## `from_awaitable`

将第三方 awaitable 包装为 cnetmod `task<T>`：

```cpp
auto value = co_await from_awaitable<int>(third_party_async_call());
co_await from_awaitable<void>(third_party_async_flush());
```

## 正确用法

| 正确 | 错误 |
|---|---|
| `co_await scheduler.schedule()` 切换到目标事件循环 | 在公开模块中暴露 sender/receiver 类型 |
| `blocking_invoke(pool, io, fn)` 桥接阻塞 API | 在 `io_context` 线程直接调用阻塞函数 |
| `pool_post_awaitable` 卸载 CPU 计算 | 在事件循环线程执行长时间计算 |
| `from_awaitable<T>` 统一第三方 awaitable | 假设所有第三方 task 与 cnetmod 生命周期一致 |
| 同步入口使用 `sync_wait(task)` | 在协程内部同步等待 task |

## 参考示例

- `examples/concurrency/stdexec_bridge.cpp`：原生 `task<T>` 与 `sync_wait`
- `examples/concurrency/blocking_bridge_demo.cpp`：阻塞卸载、并发组合、第三方 awaitable
- `examples/http/multicore_http.cpp`：`server_context` 多核 HTTP 服务
