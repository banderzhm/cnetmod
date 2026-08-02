# Coroutine 并发原语

> C++20 协程原语集合：task、channel、mutex、semaphore、wait_group、cancel_token，全部非阻塞、零堆分配设计。

**import**: `import cnetmod.coro.task;` / `import cnetmod.coro.channel;` / `import cnetmod.coro.mutex;` 等子模块
**源码**: `src/coro/task.cppm`, `spawn.cppm`, `channel.cppm`, `mutex.cppm`, `shared_mutex.cppm`, `semaphore.cppm`, `wait_group.cppm`, `cancel.cppm`

## 场景导航

- 写异步函数并同步等待 → [`task` + `sync_wait`](#taskt--sync_wait)
- 并发执行多个任务 → [`when_all`](#when_all)
- 启动即发即弃后台任务 → [`spawn`](#spawn)
- 生产者/消费者传递数据 → [`channel`](#channel)
- 保护协程共享数据 → [`async_mutex`](#async_mutex)
- 多读单写 → [`async_shared_mutex`](#async_shared_mutex)
- 限制并发数量 → [`async_semaphore`](#async_semaphore)
- 等待一组协程完成 → [`async_wait_group`](#async_wait_group)
- 取消异步操作 → [`cancel_token`](#cancel_token)

## API 参考

### `task<T>` / `sync_wait`

**签名**:
```cpp
export template <typename T> class task;       // 协程返回类型（不可拷贝，可移动）
export template <typename T> auto sync_wait(task<T> t) -> T;  // 阻塞等待
export void sync_wait(task<void> t);
```

```cpp
import std;
import cnetmod.coro.task;
using namespace cnetmod;

auto compute(int x) -> task<int> { co_return x * x; }

int main() {
    auto r = sync_wait(compute(42));  // r = 1764
    std::println("{}", r);
}
```

---

### `when_all`

**签名**:
```cpp
export template <typename T1, typename T2>
auto when_all(task<T1>, task<T2>) -> task<std::tuple<T1, T2>>;

export template <typename T1, typename T2, typename T3>
auto when_all(task<T1>, task<T2>, task<T3>) -> task<std::tuple<T1, T2, T3>>;

export template <typename... Ts> requires(sizeof...(Ts) >= 4)
auto when_all(task<Ts>...) -> task<std::tuple<Ts...>>;

// void + 非void 组合：返回非void结果
export template <typename T2> requires(!std::is_void_v<T2>)
auto when_all(task<void>, task<T2>) -> task<T2>;

export auto when_all(task<void>, task<void>) -> task<void>;
```

所有子任务**真正并发**启动（非顺序 await），全部完成后恢复调用者。

```cpp
auto [a, b] = co_await when_all(fetch_a(), fetch_b());
```

---

### `spawn`

**签名**: `export void spawn(io_context& ctx, task<void> task_to_run);`

即发即弃：投递到 `io_context` 后立即返回。未捕获异常调用 `std::terminate()`。

---

### `channel<T>`

有界异步通道，环形缓冲 + 自适应自旋锁 + 直接交接优化。

**签名**: `export template <typename T> class channel;`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit channel(std::size_t capacity = 1)` | 指定缓冲区容量 |
| send | `auto send(T value) -> send_awaitable` | `co_await ch.send(v)` → `bool`（false=已关闭） |
| receive | `auto receive() -> recv_awaitable` | `co_await ch.receive()` → `std::optional<T>` |
| try_send | `auto try_send(T value) -> bool` | 非阻塞发送 |
| try_receive | `auto try_receive() noexcept -> std::optional<T>` | 非阻塞接收 |
| try_receive_many | `auto try_receive_many(std::vector<T>&, std::size_t) -> std::size_t` | 批量接收 |
| close | `void close() noexcept` | 关闭通道，唤醒所有等待者 |
| is_closed | `auto is_closed() const noexcept -> bool` | 查询状态 |

```cpp
import std;
import cnetmod.coro.task;
import cnetmod.coro.channel;
using namespace cnetmod;

auto producer(channel<int>& ch, int n) -> task<void> {
    for (int i = 0; i < n; ++i) co_await ch.send(i);
    ch.close();
}
auto consumer(channel<int>& ch) -> task<void> {
    while (auto val = co_await ch.receive())
        std::println("recv {}", *val);
}
auto run() -> task<void> {
    channel<int> ch(2);
    co_await when_all(producer(ch, 5), consumer(ch));
}
int main() { sync_wait(run()); }
```

---

### `async_mutex`

非阻塞协程互斥锁，竞争时挂起协程而非阻塞线程。

**签名**: `export class async_mutex;`

| 方法 | 签名 | 说明 |
|------|------|------|
| lock | `auto lock() noexcept -> lock_awaitable` | `co_await mtx.lock()` |
| unlock | `void unlock() noexcept` | 释放锁 |
| try_lock | `auto try_lock() noexcept -> bool` | 非阻塞尝试 |

**RAII 守卫** — `export class async_lock_guard;`：
```cpp
co_await mtx.lock();
async_lock_guard guard(mtx, std::adopt_lock);
// ... 临界区 ...（析构自动解锁）
```

---

### `async_shared_mutex`

非阻塞协程读写锁，**写者优先**防止写者饥饿。

**签名**: `export class async_shared_mutex;`

| 方法 | 签名 | 说明 |
|------|------|------|
| lock_shared | `auto lock_shared() noexcept -> lock_shared_awaitable` | 获取共享读锁 |
| unlock_shared | `void unlock_shared() noexcept` | 释放读锁 |
| lock | `auto lock() noexcept -> lock_awaitable` | 获取独占写锁 |
| unlock | `void unlock() noexcept` | 释放写锁 |

**RAII 守卫**: `async_shared_lock_guard`（读）、`async_unique_lock_guard`（写）。

```cpp
async_shared_mutex rw;
co_await rw.lock_shared();
async_shared_lock_guard rg(rw, std::adopt_lock);
// ... 读取 ...

co_await rw.lock();
async_unique_lock_guard wg(rw, std::adopt_lock);
// ... 写入 ...
```

---

### `async_semaphore`

非阻塞协程计数信号量，限制并发数量。

**签名**: `export class async_semaphore;`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit async_semaphore(std::size_t initial_count) noexcept` | 初始许可数 |
| acquire | `auto acquire() noexcept -> acquire_awaitable` | `co_await sem.acquire()` |
| release | `void release() noexcept` / `void release(std::size_t n) noexcept` | 释放许可 |
| try_acquire | `auto try_acquire() noexcept -> bool` | 非阻塞尝试 |
| available | `auto available() const noexcept -> std::size_t` | 可用许可数 |

---

### `async_wait_group`

类似 Go `sync.WaitGroup`，等待一组协程完成。

**签名**: `export class async_wait_group;`

| 方法 | 签名 | 说明 |
|------|------|------|
| add | `void add(int n = 1) noexcept` | 增加计数 |
| done | `void done() noexcept` | 减少计数，到零唤醒等待者 |
| wait | `auto wait() noexcept -> wait_awaitable` | `co_await wg.wait()` |
| count | `auto count() const noexcept -> int` | 当前计数（仅供监控） |

---

### `cancel_token`

异步操作取消令牌。线程安全，不可拷贝/移动（地址稳定性）。

**签名**: `export class cancel_token;`

| 方法 | 签名 | 说明 |
|------|------|------|
| cancel | `void cancel() noexcept` | 请求取消（多次调用仅首次生效） |
| is_cancelled | `auto is_cancelled() const noexcept -> bool` | 是否已取消 |
| reset | `void reset() noexcept` | 重置以供复用（前提：无进行中操作） |

## Do's & Don'ts

| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 用 `sync_wait()` 在 `main()` 入口等待协程 | 在协程内部调用 `sync_wait()` |
| 用 `async_lock_guard` RAII 管理锁 | 手动 `unlock()` 忘记异常路径释放 |
| `close()` 后仍可 `receive()` 读取缓冲数据 | 假设 `close()` 后 `receive()` 立即返回 `nullopt` |
| `when_all()` 并发执行独立任务 | 用 `when_all()` 执行有依赖的任务 |
| `cancel_token` 通过 `reset()` 复用 | 拷贝或移动 `cancel_token`（已 delete） |
| `spawn()` 启动不关心结果的后台任务 | 用 `spawn()` 启动需要返回值的任务 |

## 参考示例

- `examples/concurrency/channel_demo.cpp` — channel 生产者/消费者模式
- `examples/concurrency/mutex_demo.cpp` — async_mutex 保护共享状态
