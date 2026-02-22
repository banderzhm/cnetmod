# 协程

本指南涵盖 cnetmod 的协程系统，包括 `task<T>`、`spawn()` 和 async/await 模式。

## 概述

cnetmod 使用 C++20 协程进行异步编程。协程提供：

- **零开销**：编译为状态机，无回调分配
- **自然语法**：使用 `co_await` 而不是回调地狱
- **可组合性**：易于组合异步操作
- **取消**：内置协作取消支持

## `task<T>` 类型

`task<T>` 是 cnetmod 中的基本协程类型。它表示一个惰性的异步计算，产生类型为 `T` 的值。

### 基本用法

```cpp
import cnetmod;
using namespace cnetmod;

// 定义异步函数
task<int> compute_value() {
    co_await async_sleep(ctx, 1s);
    co_return 42;
}

// 调用它
task<void> main_task(io_context& ctx) {
    int result = co_await compute_value();
    std::println("Result: {}", result);
}
```

### 关键特性

1. **惰性求值**：`task<T>` 在被等待之前不会开始执行
2. **单次使用**：只能被等待一次
3. **仅可移动**：不能被复制
4. **异常传播**：异常通过 `co_await` 传播

```cpp
task<int> may_throw() {
    if (error_condition)
        throw std::runtime_error("Error!");
    co_return 42;
}

task<void> caller() {
    try {
        int value = co_await may_throw();
    } catch (const std::exception& e) {
        std::println("Caught: {}", e.what());
    }
}
```

## `spawn()` 函数

`spawn()` 启动一个协程而不等待其结果（即发即忘）。

### 基本用法

```cpp
task<void> background_work() {
    co_await async_sleep(ctx, 5s);
    std::println("Background work done");
}

int main() {
    io_context ctx;
    
    // 启动后台任务
    spawn(ctx, background_work());
    
    // 立即继续
    ctx.run();
}
```

### 使用场景

- **并发连接**：同时处理多个客户端
- **后台任务**：定期清理、监控
- **事件处理器**：即发即忘的事件处理

```cpp
task<void> handle_client(tcp_socket client) {
    // 处理客户端连接
    char buf[1024];
    while (true) {
        int n = co_await client.async_recv(buf, sizeof(buf));
        if (n <= 0) break;
        co_await client.async_send(buf, n);
    }
}

task<void> server(io_context& ctx) {
    tcp_acceptor acceptor(ctx, 8080);
    while (true) {
        auto [client, addr] = co_await acceptor.async_accept();
        // 生成并发处理器
        spawn(ctx, handle_client(std::move(client)));
    }
}
```

## 协程关键字

### `co_await`

挂起协程直到等待的操作完成。

```cpp
// 等待 I/O
int n = co_await socket.async_recv(buffer, size);

// 等待定时器
co_await async_sleep(ctx, 2s);

// 等待另一个任务
auto result = co_await compute_value();
```

### `co_return`

从协程返回值。

```cpp
task<int> get_value() {
    co_return 42;
}

task<void> no_value() {
    // 执行工作
    co_return;  // 或直接到达函数末尾
}
```

### `co_yield`

在 cnetmod 中不使用（我们使用 `channel<T>` 作为生成器）。

## 对称传输

cnetmod 使用对称传输进行尾调用优化，防止栈增长。

```cpp
task<int> recursive_task(int n) {
    if (n == 0)
        co_return 0;
    
    // 这不会增长栈！
    int result = co_await recursive_task(n - 1);
    co_return result + 1;
}
```

**工作原理**：不是恢复调用者的协程帧，而是直接将控制权转移到下一个协程，避免栈堆积。

## 协程生命周期

### 自动清理

协程帧在以下情况下自动销毁：
- 协程正常完成
- 异常传播出去
- 协程被取消

```cpp
task<void> example() {
    tcp_socket socket(ctx);  // RAII 资源
    co_await socket.async_connect(addr);
    // 协程结束时 socket 自动关闭
}
```

### 分离任务

`spawn()` 创建一个管理自己生命周期的分离任务：

```cpp
spawn(ctx, []() -> task<void> {
    co_await async_sleep(ctx, 1s);
    std::println("Done");
}());
// 即使 spawn() 调用者退出，协程也会继续
```

## 取消

使用 `cancel_token` 进行协作取消：

```cpp
task<void> cancellable_operation(cancel_token token) {
    while (!token.is_cancelled()) {
        co_await do_work();
    }
    std::println("Operation cancelled");
}

// 使用
cancel_token token;
spawn(ctx, cancellable_operation(token));

// 稍后：取消操作
token.cancel();
```

## 常见模式

### 顺序操作

```cpp
task<void> sequential() {
    auto result1 = co_await operation1();
    auto result2 = co_await operation2(result1);
    auto result3 = co_await operation3(result2);
    co_return result3;
}
```

### 并发操作

```cpp
task<void> concurrent(io_context& ctx) {
    // 启动两个操作
    spawn(ctx, operation1());
    spawn(ctx, operation2());
    
    // 或使用 wait_group 进行同步
    wait_group wg;
    wg.add(2);
    
    spawn(ctx, [&]() -> task<void> {
        co_await operation1();
        wg.done();
    }());
    
    spawn(ctx, [&]() -> task<void> {
        co_await operation2();
        wg.done();
    }());
    
    co_await wg.wait();
}
```

### 超时模式

```cpp
task<void> with_timeout_example(io_context& ctx) {
    try {
        auto result = co_await with_timeout(ctx, slow_operation(), 5s);
        std::println("Success: {}", result);
    } catch (const timeout_error&) {
        std::println("Operation timed out");
    }
}
```

### 重试模式

```cpp
task<int> retry_operation(io_context& ctx) {
    retry_policy policy{
        .max_attempts = 3,
        .initial_delay = 100ms,
        .max_delay = 5s,
        .backoff_multiplier = 2.0
    };
    
    co_return co_await retry(ctx, policy, []() -> task<int> {
        co_return co_await unreliable_operation();
    });
}
```

## 性能考虑

### 协程帧大小

- 典型的 `task<T>` 帧：约 64 字节
- 包括：promise 对象、局部变量、挂起点
- 在堆上分配（每个协程一次）

### 挂起/恢复成本

- 对称传输：2-3 个 CPU 周期
- 无函数调用开销
- 无栈操作

### 内存分配

```cpp
// 每个协程单次分配
task<void> example() {
    int local = 42;  // 协程帧的一部分
    co_await operation();
}
```

避免分配的方法：
- 小协程帧（少量局部变量）
- 对大对象使用移动语义
- 使用 `span<>` 和 `string_view` 而不是复制

## 调试协程

### 常见问题

**1. 悬空引用**

```cpp
// 错误：引用临时对象
task<void> bad_example() {
    std::string temp = "hello";
    co_await operation();
    // 如果协程被移动，temp 可能被销毁
}

// 正确：值或移动
task<void> good_example() {
    std::string value = "hello";  // 存储在协程帧中
    co_await operation();
}
```

**2. 忘记 `co_await`**

```cpp
// 错误：任务未被等待
task<void> bad() {
    compute_value();  // 什么都不做！
}

// 正确：等待任务
task<void> good() {
    co_await compute_value();
}
```

**3. 等待已移动的任务**

```cpp
// 错误：任务已移动
task<void> bad() {
    auto t = compute_value();
    auto t2 = std::move(t);
    co_await t;  // 未定义行为！
}
```

### 调试技巧

1. **使用清理器**：AddressSanitizer 捕获释放后使用
2. **启用日志**：在挂起/恢复点添加调试打印
3. **检查生命周期**：确保对象的生命周期长于协程
4. **使用 RAII**：让析构函数处理清理

## 与阻塞代码集成

### 从同步调用异步

```cpp
int sync_function() {
    io_context ctx;
    return blocking_invoke(ctx, async_operation());
}
```

### 从异步调用同步

```cpp
task<int> async_function(io_context& ctx) {
    // 将阻塞工作卸载到线程池
    co_return co_await blocking_invoke(ctx, [] {
        return expensive_computation();
    });
}
```

## 最佳实践

1. **始终 `co_await` 任务**：不要丢弃任务结果
2. **使用 `spawn()` 进行即发即忘**：意图清晰
3. **优先使用 RAII**：让析构函数管理资源
4. **避免引用捕获**：使用值或移动
5. **使用 `cancel_token`**：用于优雅关闭
6. **保持帧小**：最小化局部变量
7. **使用 `span<>` 和 `string_view`**：避免复制

## 下一步

- **[I/O 上下文](io-context.md)** - 事件循环和调度
- **[同步原语](sync-primitives.md)** - Mutex、channel、semaphore
- **[TCP/UDP 指南](../protocols/tcp-udp.md)** - 网络编程
- **[HTTP 服务器](../protocols/http.md)** - 构建 Web 服务
