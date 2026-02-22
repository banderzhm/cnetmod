# 协程桥接模块 (cnetmod.coro.bridge)

## 问题背景

在基于协程的异步服务器中，经常需要对接只提供**阻塞/多线程 API** 的第三方库（如 RabbitMQ C 客户端、gRPC 同步 stub、传统数据库驱动），或者与 **stdexec sender/receiver** 生态互操作。如果在 `task<T>` 协程中直接调用这些阻塞 API，会卡死 `io_context` 事件循环线程，导致整个服务器停止响应。

`bridge.cppm` 提供三种桥接工具，完整解决这些跨世界互操作问题。

## 模块导入

```cpp
import cnetmod.coro.bridge;
```

该模块依赖：`cnetmod.coro.task`、`cnetmod.io.io_context`、`cnetmod.executor.pool`。

---

## 工具一览

| 工具 | 签名 | 用途 |
|------|------|------|
| `blocking_invoke` | `(pool, io, fn) → task<R>` | 阻塞调用卸载到线程池 |
| `await_sender<T>` | `(sender) → awaitable` | co_await 任意 stdexec sender |
| `from_awaitable<T>` | `(awaitable) → task<T>` | 包装第三方协程 awaitable |

---

## 1. blocking_invoke — 阻塞 API 桥接

### 原理

```
io_context 线程          线程池线程          io_context 线程
      │                                         
      ├─ co_await ─────► pool_post_awaitable     
      │                     │                    
      │                     ├─ fn()  (阻塞执行)   
      │                     │                    
      │  post_awaitable ◄───┤                    
      │                                          
      ├─ co_return result ◄──────────────────────┘
```

协程先挂起并切换到 stdexec 线程池执行阻塞操作，完成后自动通过 `post_awaitable` 切回 `io_context` 事件循环线程，整个过程对调用者透明。

### 函数签名

```cpp
// 有返回值版本
template <typename F>
    requires std::invocable<std::decay_t<F>>
          && (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>)
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn)
    -> task<std::invoke_result_t<std::decay_t<F>>>;

// void 返回值版本
template <typename F>
    requires std::invocable<std::decay_t<F>>
          && std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
auto blocking_invoke(exec::static_thread_pool& pool, io_context& io, F&& fn)
    -> task<void>;
```

### 用法

```cpp
auto handle_message(server_context& ctx) -> task<void> {
    auto& pool = ctx.pool();
    auto& io   = ctx.accept_io();

    // RabbitMQ 阻塞消费 → 线程池执行，不阻塞事件循环
    auto msg = co_await blocking_invoke(pool, io, [&] {
        return rabbitmq_client.basic_consume("orders", 5000);
    });

    // gRPC 同步调用
    auto reply = co_await blocking_invoke(pool, io, [&] {
        return grpc_stub.SayHello(request);
    });

    // void 返回值也支持
    co_await blocking_invoke(pool, io, [&] {
        rabbitmq_client.basic_publish("exchange", "routing_key", payload);
    });
}
```

### 并发执行多个阻塞操作

配合 `when_all` 可以并发执行多个阻塞操作，总耗时等于最慢的那个，而不是顺序累加：

```cpp
// 总时间 ≈ max(200ms, 100ms) = 200ms，而不是 300ms
auto [msg, rows] = co_await when_all(
    blocking_invoke(pool, io, [] {
        return rabbitmq.consume("events", 200);  // 200ms
    }),
    blocking_invoke(pool, io, [] {
        return db.query("SELECT * FROM orders");  // 100ms
    })
);
```

---

## 2. await_sender — co_await stdexec sender

### 原理

`sender_awaitable<T, Sender>` 是一个 awaitable 适配器，内部实现：

1. `await_suspend`: 调用 `stdexec::connect(sender, bridge_receiver)` 构造 `op_state`，然后 `start`
2. sender 完成时：`bridge_receiver` 将结果存储并 `resume()` 协程
3. `await_resume`: 返回存储的结果（或重新抛出异常）

`op_state` 使用 placement storage 存放在 awaitable 对象中（awaitable 嵌入协程帧，挂起期间生命周期安全）。

### 函数签名

```cpp
// 工厂函数 — T 需要显式指定
template <typename T, typename Sender>
auto await_sender(Sender&& sndr) -> sender_awaitable<T, std::decay_t<Sender>>;
```

### 用法

```cpp
// co_await 一个 task_sender<int>（task<T> 转换的 stdexec sender）
auto val = co_await await_sender<int>(as_sender(compute(42)));

// co_await 一个 void sender
co_await await_sender<void>(as_sender(fire_and_forget()));

// 也可以 co_await 任何其他符合 stdexec sender 概念的对象
// 例如 io_scheduler::schedule() 产生的 sender
auto sched = io_scheduler{io_ctx};
co_await await_sender<void>(sched.schedule());
```

### sender_awaitable 底层类型

如果需要更精细的控制，可以直接使用 `sender_awaitable<T, Sender>` 类型：

```cpp
auto sndr = /* some stdexec sender */;
auto aw = sender_awaitable<int, decltype(sndr)>{std::move(sndr)};
auto result = co_await std::move(aw);
```

通常推荐使用 `await_sender<T>(sndr)` 工厂函数，自动推导 Sender 类型。

---

## 3. from_awaitable — 包装第三方 awaitable

### 原理

将任意实现了 `await_ready / await_suspend / await_resume` 接口的类型（或实现了 `operator co_await()` 的类型）包装为 cnetmod `task<T>`。

实现极为简洁：

```cpp
template <typename T, typename Awaitable>
auto from_awaitable(Awaitable&& aw) -> task<T> {
    if constexpr (std::is_void_v<T>) {
        co_await std::forward<Awaitable>(aw);
    } else {
        co_return co_await std::forward<Awaitable>(aw);
    }
}
```

### 用法

```cpp
// 假设第三方库返回了自己的 awaitable 类型
auto result = co_await from_awaitable<int>(third_party_async_call());

// void 类型
co_await from_awaitable<void>(third_party_fire_and_forget());
```

> **注意**：cnetmod 的 `task<T>::promise_type` 天然支持 `co_await` 任意 awaitable（通过 `await_transform` 透传），所以大多数情况下可以直接 `co_await third_party_awaitable{}`，无需显式使用 `from_awaitable`。`from_awaitable` 主要用于需要**类型擦除**的场景 —— 将不同类型的 awaitable 统一为 `task<T>` 以便存储、传递或用于 `when_all` 等组合器。

---

## 完整示例

见 `examples/blocking_bridge_demo.cpp`，演示了四种场景：

1. **blocking_invoke** — 模拟 RabbitMQ 消费/发布、数据库查询
2. **when_all + blocking_invoke** — 并发执行多个阻塞操作
3. **await_sender** — co_await stdexec `task_sender`
4. **from_awaitable** — 包装外部 awaitable 类型

运行方式：

```bash
cmake --build build --target example_blocking_bridge_demo
./build/examples/Debug/example_blocking_bridge_demo    # Windows
./build/examples/example_blocking_bridge_demo           # Linux/macOS
```

---

## 典型应用架构

```
┌─────────────────────────────────────────────────────┐
│                   协程 task<T> 世界                   │
│                                                     │
│  HTTP handler ──► blocking_invoke ──► RabbitMQ (阻塞) │
│       │                                              │
│       ├────────► blocking_invoke ──► gRPC sync (阻塞) │
│       │                                              │
│       ├────────► await_sender ───► stdexec pipeline   │
│       │                                              │
│       └────────► from_awaitable ► 第三方协程库 task    │
│                                                     │
├────────────── io_context (事件循环线程) ──────────────┤
├────────────── exec::static_thread_pool ──────────────┤
└─────────────────────────────────────────────────────┘
```

**关键保证**：

- `blocking_invoke` 执行完毕后，协程一定回到 `io_context` 线程，可安全继续异步 I/O 操作
- `sender_awaitable` 的 `op_state` 生命周期与协程帧绑定，不会悬空
- 所有桥接工具都正确传播异常（`std::exception_ptr`）
