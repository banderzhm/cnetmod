# 协程桥接模块 (`cnetmod.coro.bridge`)

## 用途

`cnetmod.coro.bridge` 用于把阻塞 API 和第三方 C++ awaitable 接入 cnetmod
协程运行时，同时不在模块接口中暴露第三方执行器类型。

```cpp
import cnetmod.coro.bridge;
```

公开 API 只使用 cnetmod 自有类型。执行器的具体实现保留在普通 `.cpp`
文件中，应用侧不需要导入或依赖 stdexec 实现细节。

## API 一览

| API | 用途 |
|---|---|
| `blocking_invoke(pool, io, fn)` | 在线程池执行阻塞调用，完成后回到指定 `io_context` |
| `from_awaitable<T>(awaitable)` | 将第三方 C++ awaitable 适配为 `task<T>` |

## 阻塞调用

第三方库只有同步 API 时，使用 `blocking_invoke`。调用体在 `thread_pool`
线程执行，完成后协程会回到传入的 `io_context`。

```cpp
auto load_user(server_context& server, io_context& io) -> task<user>
{
    co_return co_await blocking_invoke(server.pool(), io, []
        {
            return legacy_database.load_user(42);
        });
}
```

有返回值和 `void` 调用都支持。互不依赖的阻塞操作可以通过 `when_all`
并发执行：

```cpp
auto [message, rows] = co_await when_all(
    blocking_invoke(pool, io, [] { return queue.consume(); }),
    blocking_invoke(pool, io, [] { return database.query(); }));
```

阻塞调用体运行在线程池线程，不应在其中访问只能由 `io_context` 线程持有的
对象。等 `blocking_invoke` 返回后，再继续事件循环相关操作。

## 第三方 Awaitable

`from_awaitable<T>` 可以把兼容的 C++ awaitable 转为 cnetmod `task<T>`：

```cpp
auto value = co_await from_awaitable<int>(third_party_async_call());
co_await from_awaitable<void>(third_party_async_flush());
```

多数第三方 awaitable 也可以直接 `co_await`。当接口要求明确的 `task<T>`，
或者需要统一组合不同 awaitable 类型时，再使用该适配器。

## 原生调度

`io_scheduler` 是协程调度门面，不是 sender。`co_await schedule()` 会把当前
协程投递到指定 `io_context`：

```cpp
import cnetmod.executor.scheduler;

auto continue_on(io_context& io) -> task<void>
{
    io_scheduler scheduler{io};
    co_await scheduler.schedule();
    // 此处已经通过 io 的 post 队列恢复。
}
```

CPU 任务使用 `pool_post_awaitable` 或 `blocking_invoke`；同步入口使用 task
运行时原生的 `sync_wait`。

## 示例

参见 `examples/concurrency/blocking_bridge_demo.cpp`，其中包含阻塞调用卸载、
并发卸载、原生 task 组合和第三方 awaitable 示例。

关键保证：

- cnetmod 模块 BMI 不导出 stdexec 类型；
- 阻塞任务不会占用事件循环线程；
- `blocking_invoke` 完成后回到指定 `io_context`；
- 调用体和 awaitable 的异常通过 `task<T>` 传播。
