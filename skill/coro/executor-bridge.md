# Executor 与 Bridge

> stdexec 调度器集成、线程池、多核服务器上下文，以及阻塞操作 / sender / awaitable 三者之间的桥接工具。

**import**: `import cnetmod.executor.scheduler;` / `import cnetmod.executor.pool;` / `import cnetmod.coro.bridge;`
**源码**: `src/executor/scheduler.cppm`, `src/executor/pool.cppm`, `src/coro/bridge.cppm`

## 场景导航

- 我要把 `task<T>` 接入 stdexec sender 管道 → [看这里](#task_sendert--as_sender)
- 我要同步等待一个 sender 完成 → [看这里](#sync_wait_sender)
- 我要把 CPU 密集操作卸载到线程池 → [看这里](#pool_post_awaitable)
- 我要构建多核服务器（accept + N worker） → [看这里](#server_context)
- 我要桥接阻塞 API（RabbitMQ / gRPC 同步客户端）到协程 → [看这里](#blocking_invoke)
- 我要在协程中 `co_await` 一个 stdexec sender → [看这里](#await_sender)
- 我要把第三方 awaitable 包装为 `task<T>` → [看这里](#from_awaitable)

## API 参考

### `io_scheduler`

stdexec 兼容的 scheduler，将 `io_context` 事件循环暴露为 stdexec 调度器。

**签名**:
```cpp
export class io_scheduler
{
public:
    using scheduler_concept = stdexec::scheduler_t;

    explicit io_scheduler(io_context& ctx) noexcept;
    auto operator==(const io_scheduler&) const noexcept -> bool;
    [[nodiscard]] auto context() const noexcept -> io_context&;
    auto schedule() noexcept -> schedule_sender;
};
```

**参数**:
- `ctx` — 绑定的 `io_context` 事件循环

**示例**:
```cpp
import std;
import cnetmod.executor.scheduler;
import cnetmod.io.io_context;

using namespace cnetmod;

auto ctx = make_io_context();
io_scheduler sched(*ctx);
auto sender = sched.schedule();  // 返回 schedule_sender，post 到事件循环
```

---

### `task_sender<T>` / `as_sender`

将 `task<T>` 包装为 stdexec sender，可接入 `stdexec::then` / `stdexec::upon_error` 等管道。

**签名**:
```cpp
export template <typename T> class task_sender
{
public:
    using sender_concept = stdexec::sender_t;
    explicit task_sender(task<T> t) noexcept;
    template <typename Receiver>
    auto connect(Receiver rcvr) && -> task_op_state<T, Receiver>;
};

export template <typename T> auto as_sender(task<T> t) -> task_sender<T>;
```

**示例**:
```cpp
import std;
import cnetmod.coro.task;
import cnetmod.executor.scheduler;

using namespace cnetmod;

auto compute(int x) -> task<int> { co_return x * x; }

auto sender = as_sender(compute(7));  // task_sender<int>
```

---

### `sync_wait_sender`

同步运行 `task_sender<T>`，阻塞当前线程直到 sender 完成，返回结果值。

**签名**:
```cpp
export template <typename T>
auto sync_wait_sender(task_sender<T>&& sender) -> T;
```

**示例**:
```cpp
import std;
import cnetmod.coro.task;
import cnetmod.executor.scheduler;

using namespace cnetmod;

auto compute(int x, int y) -> task<int> { co_return x * y + 1; }

int main() {
    auto result = sync_wait_sender(as_sender(compute(2, 5)));
    std::println("compute(2,5) = {}", result);  // 输出 11
}
```

---

### `thread_pool`

统一线程池类型别名，底层为 `exec::static_thread_pool`。下游代码只依赖 `cnetmod::thread_pool`，升级 stdexec 只需改此处。

**签名**:
```cpp
export using thread_pool = exec::static_thread_pool;
```

---

### `pool_post_awaitable`

将当前协程切换到 stdexec 线程池线程执行。`co_await` 后协程在线程池线程上恢复，适合卸载 CPU 密集操作。

**签名**:
```cpp
export struct pool_post_awaitable
{
    thread_pool& pool;
    explicit pool_post_awaitable(thread_pool& p) noexcept;
    auto await_ready() const noexcept -> bool;        // 始终返回 false
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept;
};
```

**示例**:
```cpp
import std;
import cnetmod.coro.task;
import cnetmod.executor.pool;

using namespace cnetmod;

auto heavy_compute(server_context& sctx, int n) -> task<std::uint64_t> {
    // 切换到线程池
    co_await pool_post_awaitable{sctx.pool()};

    // CPU 密集计算在线程池线程上执行
    std::uint64_t result = /* ... heavy work ... */ n * n;

    // 切回 io_context 线程
    co_await post_awaitable{ctx};
    co_return result;
}
```

---

### `spawn_on`

将协程投递到指定 `io_context`（跨线程安全），语义同 `spawn(ctx, t)` 但显式用于跨线程场景。

**签名**:
```cpp
export void spawn_on(io_context& target, task<void> t);
```

---

### `server_context`

多核服务器上下文，管理 accept 专用 `io_context` + N 个 worker `io_context` + stdexec 线程池。

**架构**:
- 线程 0（主线程）：`accept_io()` — 运行 accept 循环
- 线程 1..N：`worker_io` — 每个线程一个 `io_context`，处理连接 I/O
- `exec::static_thread_pool`：可选的 CPU 密集操作卸载

**签名**:
```cpp
export class server_context
{
public:
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    [[nodiscard]] auto accept_io() noexcept -> io_context&;
    [[nodiscard]] auto next_worker_io() noexcept -> io_context&;
    [[nodiscard]] auto worker_count() const noexcept -> unsigned;
    [[nodiscard]] auto worker_ios() -> std::vector<io_context*>;
    [[nodiscard]] auto pool() noexcept -> thread_pool&;

    template <typename F> requires std::invocable<std::decay_t<F>>
    auto offload(io_context& return_to, F&& fn);

    void spawn_next(task<void> t);
    void run();
    void stop();
};
```

**参数**:
- `workers` — worker 线程数，默认为 CPU 核心数
- `pool_threads` — stdexec 线程池大小，默认为 CPU 核心数

| 方法 | 说明 |
|------|------|
| `accept_io()` | 获取专用 accept 的 `io_context` |
| `next_worker_io()` | 原子 round-robin 选择下一个 worker `io_context` |
| `pool()` | 获取 stdexec 线程池引用 |
| `offload(io, fn)` | 卸载阻塞操作到线程池，完成后切回 `io` 线程 |
| `spawn_next(t)` | 在下一个 worker 上启动协程（round-robin） |
| `run()` | 启动 worker 线程，在当前线程运行 `accept_io`，阻塞直到 `stop()` |
| `stop()` | 停止所有 `io_context` 和线程池 |

**示例**:
```cpp
import std;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

using namespace cnetmod;

int main() {
    server_context sctx(4 /*workers*/, 4 /*pool_threads*/);

    spawn(sctx.accept_io(), run_server(sctx));
    sctx.run();  // 阻塞直到 stop()
}
```

---

### `blocking_invoke`

将阻塞调用卸载到 stdexec 线程池，完成后自动切回 `io_context` 事件循环线程。适用于 RabbitMQ、gRPC 同步客户端、传统数据库驱动等仅提供阻塞 API 的库。

**签名**:
```cpp
export template <typename F>
requires std::invocable<std::decay_t<F>> && (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>)
auto blocking_invoke(thread_pool& pool, io_context& io, F&& fn);
// 返回 task<R>，R = fn() 的返回值类型

// void 返回值特化
export template <typename F>
requires std::invocable<std::decay_t<F>> && std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
auto blocking_invoke(thread_pool& pool, io_context& io, F&& fn);
// 返回 task<void>
```

**原理**:
1. `co_await pool_post_awaitable` → 协程挂起，在线程池线程恢复
2. 执行 `fn()` → 阻塞操作在线程池线程运行，不影响 `io_context`
3. `co_await post_awaitable` → 协程挂起，在 `io_context` 线程恢复
4. `co_return result` → 调用者在 `io_context` 线程上拿到结果

**示例**:
```cpp
import std;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

using namespace cnetmod;

auto consume_messages(server_context& ctx) -> task<void> {
    auto& pool = ctx.pool();
    auto& io   = ctx.accept_io();

    // 阻塞 RabbitMQ 消费（在线程池线程执行）
    auto msg = co_await blocking_invoke(pool, io, [] {
        return rabbitmq_client.consume("orders", 200);
    });
    std::println("received: {}", msg);

    // 阻塞 publish（void 返回值）
    co_await blocking_invoke(pool, io, [] {
        rabbitmq_client.publish("notifications", "order_created");
    });

    // 可与 when_all 组合实现并发阻塞调用
    auto [msg2, rows] = co_await when_all(
        blocking_invoke(pool, io, [] { return mq.consume("events", 200); }),
        blocking_invoke(pool, io, [] { return db.query("SELECT * FROM orders"); })
    );
}
```

---

### `await_sender`

在 `task<T>` 协程中 `co_await` 任意 stdexec sender，实现 cnetmod 协程与 stdexec sender 生态的互操作。

**签名**:
```cpp
export template <typename T, typename Sender>
auto await_sender(Sender&& sndr);
// 返回 sender_awaitable<T, Sender>，co_await 结果为 T
```

**参数**:
- `T` — sender 完成时发送的值类型（需显式指定）
- `sndr` — 任意 stdexec sender

**示例**:
```cpp
import std;
import cnetmod.coro.task;
import cnetmod.executor.scheduler;
import cnetmod.coro.bridge;

using namespace cnetmod;

auto demo() -> task<void> {
    auto sched = io_scheduler{ctx};

    // co_await 一个 stdexec sender 管道
    auto val = co_await await_sender<int>(
        stdexec::then(sched.schedule(), [] { return 42; }));
    std::println("sender result: {}", val);

    // co_await task_sender（通过 as_sender 转换）
    auto msg = co_await await_sender<std::string>(as_sender(greet()));
    std::println("msg: {}", msg);

    // void sender
    co_await await_sender<void>(as_sender(fire_and_forget()));
}
```

---

### `from_awaitable`

将第三方协程库的 awaitable 类型包装为 cnetmod `task<T>`。适用于 folly::coro::Task 等其他实现了 `operator co_await()` 的类型。

**签名**:
```cpp
export template <typename T, typename Awaitable>
auto from_awaitable(Awaitable&& aw);
// 返回 task<T>，T = awaitable 的 co_await 结果类型
```

**参数**:
- `T` — awaitable 的 `await_resume()` 返回类型（需显式指定）
- `aw` — 任意 C++20 awaitable

**示例**:
```cpp
import std;
import cnetmod.coro.bridge;

using namespace cnetmod;

auto demo() -> task<void> {
    // 包装第三方 awaitable 为 task<int>
    auto result = co_await from_awaitable<int>(third_party_async_call());
    std::println("result: {}", result);

    // void 返回值
    co_await from_awaitable<void>(third_party_fire_and_forget());
}
```

## Do's & Don'ts

| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 用 `blocking_invoke(pool, io, fn)` 桥接阻塞 API | 在协程中直接调用阻塞函数（会阻塞 `io_context` 线程） |
| 用 `pool_post_awaitable` 卸载 CPU 密集计算 | 在 `io_context` 线程上做大量计算（阻塞事件循环） |
| 用 `server_context` 管理多核 accept + worker 架构 | 手动创建多个 `io_context` 和线程并管理生命周期 |
| 用 `await_sender<T>` 在协程中消费 stdexec sender | 混用不同协程框架的 awaitable 而不做桥接 |
| 用 `from_awaitable<T>` 接入第三方协程库 | 假设第三方 awaitable 可以直接 `co_await` 到 `task<T>` |
| `sync_wait_sender` 仅在 `main()` 等同步入口使用 | 在协程内部调用 `sync_wait_sender`（死锁） |
| `blocking_invoke` 配合 `when_all` 并发多个阻塞调用 | 顺序 `co_await` 多个独立的阻塞调用（浪费线程池资源） |

## 参考示例

- `examples/concurrency/stdexec_bridge.cpp` — `as_sender` + `sync_wait_sender` 桥接演示
- `examples/concurrency/blocking_bridge_demo.cpp` — `blocking_invoke` / `await_sender` / `from_awaitable` 完整示例
- `examples/http/multicore_http.cpp` — `server_context` 多核 HTTP 服务器 + `pool_post_awaitable` CPU 卸载
