# I/O 上下文

`io_context` 是 cnetmod 异步 I/O 系统的核心。它提供事件循环、任务调度和平台特定的 I/O 多路复用。

## 概述

`io_context` 的职责：
- **事件循环**：等待 I/O 事件和定时器
- **任务调度**：在 I/O 完成时执行协程
- **线程安全提交**：从任何线程提交任务
- **平台抽象**：跨 Windows/Linux/macOS 的统一 API

## 基本用法

### 创建和运行

```cpp
import cnetmod;
using namespace cnetmod;

int main() {
    // 创建 I/O 上下文
    io_context ctx;
    
    // 调度工作
    spawn(ctx, my_async_task(ctx));
    
    // 运行事件循环（阻塞直到停止）
    ctx.run();
}
```

### 停止事件循环

```cpp
task<void> shutdown_task(io_context& ctx) {
    co_await async_sleep(ctx, 10s);
    ctx.stop();  // 停止事件循环
}

int main() {
    io_context ctx;
    spawn(ctx, shutdown_task(ctx));
    ctx.run();  // 10 秒后返回
}
```

## 平台后端

cnetmod 为每个平台使用最佳的 I/O 机制：

| 平台 | 后端 | 描述 |
|----------|---------|-------------|
| Windows | **IOCP** | I/O 完成端口 - 高性能异步 I/O |
| Linux | **io_uring** | 现代异步 I/O，带内核提交队列 |
| Linux | **epoll** | io_uring 不可用时的后备方案 |
| macOS | **kqueue** | BSD 内核事件通知 |

### 后端选择

编译时自动选择：
```cpp
#ifdef CNETMOD_HAS_IOCP
    // Windows IOCP 实现
#elif defined(CNETMOD_HAS_IO_URING)
    // Linux io_uring 实现
#elif defined(CNETMOD_HAS_KQUEUE)
    // macOS kqueue 实现
#elif defined(CNETMOD_HAS_EPOLL)
    // Linux epoll 实现
#endif
```

## 线程模型

### 单线程

每个线程一个 `io_context`：

```cpp
int main() {
    io_context ctx;
    
    // 所有任务在此线程上运行
    spawn(ctx, task1(ctx));
    spawn(ctx, task2(ctx));
    spawn(ctx, task3(ctx));
    
    ctx.run();  // 在当前线程上运行所有任务
}
```

### 多线程

使用 `server_context` 实现多核：

```cpp
int main() {
    // 1 个接受线程 + 4 个工作线程
    server_context srv_ctx(4);
    
    http::server srv(srv_ctx.get_io_context());
    srv.listen(8080);
    
    srv_ctx.run();  // 阻塞直到停止
}
```

**架构**：
```
接受线程 (io_context)
    ↓
    ├─→ 工作者 1 (io_context)
    ├─→ 工作者 2 (io_context)
    ├─→ 工作者 3 (io_context)
    └─→ 工作者 4 (io_context)
```

## 任务调度

### `post()` - 线程安全提交

从任何线程提交协程：

```cpp
void worker_thread(io_context& ctx) {
    // 从不同线程调用
    ctx.post([&]() -> task<void> {
        co_await do_work(ctx);
    }().handle());
}

int main() {
    io_context ctx;
    
    std::thread worker([&] { worker_thread(ctx); });
    
    ctx.run();
    worker.join();
}
```

### `spawn()` - 便捷包装器

```cpp
spawn(ctx, my_task(ctx));

// 等价于：
ctx.post(my_task(ctx).handle());
```

### 唤醒机制

`post()` 如何唤醒事件循环：

**Windows (IOCP)**：
```cpp
PostQueuedCompletionStatus(iocp_handle, 0, WAKE_KEY, nullptr);
```

**Linux (io_uring)**：
```cpp
// 写入管道，io_uring 读取它
write(wake_pipe[1], &dummy, 1);
```

**macOS (kqueue)**：
```cpp
// 触发 eventfd
uint64_t value = 1;
write(eventfd, &value, sizeof(value));
```

## 事件循环内部

### `run()` 循环

简化的伪代码：

```cpp
void io_context::run() {
    while (!stopped_) {
        // 1. 处理就绪任务
        while (auto* task = ready_queue_.pop()) {
            task->resume();
        }
        
        // 2. 等待 I/O 事件（平台特定）
        auto events = wait_for_events(timeout);
        
        // 3. 处理完成的 I/O
        for (auto& event : events) {
            auto* op = get_async_op(event);
            op->complete();  // 恢复等待的协程
        }
    }
}
```

### 超时处理

```cpp
// 带超时等待
auto timeout = calculate_next_timer();
wait_for_events(timeout);

// 检查过期的定时器
process_expired_timers();
```

## 异步操作

### I/O 工作原理

1. **启动**：向内核提交 I/O 操作
2. **挂起**：协程挂起，事件循环继续
3. **完成**：内核发出完成信号
4. **恢复**：事件循环恢复协程

```cpp
task<int> read_example(tcp_socket& sock) {
    char buffer[1024];
    
    // 1. 启动读取
    int n = co_await sock.async_recv(buffer, sizeof(buffer));
    //         ↑
    //         在此挂起，返回事件循环
    
    // 2. 事件循环等待完成
    // 3. 内核发出数据就绪信号
    // 4. 协程在此恢复
    
    std::println("Read {} bytes", n);
    co_return n;
}
```

### 平台特定实现

**Windows (IOCP)**：
```cpp
struct async_recv_op : async_op {
    OVERLAPPED overlapped{};
    
    void start() {
        WSARecv(socket, &buffer, 1, nullptr, &flags, &overlapped, nullptr);
    }
    
    void complete() {
        // 由 IOCP 完成调用
        ctx_.post(awaiting_coroutine_);
    }
};
```

**Linux (io_uring)**：
```cpp
struct async_recv_op : async_op {
    io_uring_sqe* sqe;
    
    void start() {
        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe, socket, buffer, size, 0);
        io_uring_sqe_set_data(sqe, this);
        io_uring_submit(&ring);
    }
    
    void complete() {
        // 收到 CQE 时调用
        ctx_.post(awaiting_coroutine_);
    }
};
```

## 定时器

### `async_sleep()`

休眠一段时间：

```cpp
task<void> delayed_task(io_context& ctx) {
    std::println("Starting...");
    co_await async_sleep(ctx, 2s);
    std::println("2 seconds later");
}
```

### `async_sleep_until()`

休眠到特定时间点：

```cpp
task<void> scheduled_task(io_context& ctx) {
    auto target = std::chrono::system_clock::now() + 1h;
    co_await async_sleep_until(ctx, target);
    std::println("One hour has passed");
}
```

### `with_timeout()`

为任何操作添加超时：

```cpp
task<void> timeout_example(io_context& ctx) {
    try {
        auto result = co_await with_timeout(ctx, slow_operation(), 5s);
        std::println("Success: {}", result);
    } catch (const timeout_error&) {
        std::println("Operation timed out");
    }
}
```

### 定时器实现

定时器使用按过期时间排序的优先队列（最小堆）：

```cpp
struct timer_entry {
    time_point expiration;
    std::coroutine_handle<> awaiting;
};

std::priority_queue<timer_entry> timers_;

// 在事件循环中：
auto next_expiration = timers_.top().expiration;
auto timeout = next_expiration - now();
wait_for_events(timeout);
```

## 性能特征

### 事件循环开销

- **IOCP**：每个事件约 1-2 μs
- **io_uring**：每个事件约 0.5-1 μs
- **epoll**：每个事件约 1-2 μs
- **kqueue**：每个事件约 1-2 μs

### 可扩展性

测试配置：
- **连接数**：100K+ 并发连接
- **吞吐量**：1M+ 请求/秒（HTTP 纯文本）
- **延迟**：p50: 0.5ms，p99: 2ms

### 内存使用

每个 `io_context`：
- 基础：约 1 KB
- 每个连接：约 200 字节（socket + async_op）
- 每个定时器：约 32 字节

## 线程安全

### 线程安全操作

- ✅ `post()` - 可从任何线程调用
- ✅ `stop()` - 可从任何线程调用
- ✅ `wake()` - 内部使用，线程安全

### 非线程安全操作

- ❌ `run()` - 必须仅从一个线程调用
- ❌ Socket 操作 - 必须在同一 `io_context` 线程上
- ❌ 协程恢复 - 必须在同一线程上

### 安全的多线程模式

```cpp
class thread_safe_server {
    io_context ctx_;
    std::thread thread_;
    
public:
    void start() {
        thread_ = std::thread([this] { ctx_.run(); });
    }
    
    void submit_task(task<void> t) {
        // 线程安全：从任何线程 post
        spawn(ctx_, std::move(t));
    }
    
    void stop() {
        ctx_.stop();
        thread_.join();
    }
};
```

## 最佳实践

### 1. 每个线程一个 `io_context`

```cpp
// 好：每个线程有自己的 io_context
std::vector<std::thread> threads;
for (int i = 0; i < 4; ++i) {
    threads.emplace_back([] {
        io_context ctx;
        // ... 设置工作 ...
        ctx.run();
    });
}
```

### 2. 使用 `server_context` 实现多核

```cpp
// 好：自动负载均衡
server_context srv_ctx(std::thread::hardware_concurrency());
```

### 3. 不要阻塞事件循环

```cpp
// 错误：阻塞事件循环
task<void> bad_example(io_context& ctx) {
    std::this_thread::sleep_for(1s);  // 阻塞！
}

// 正确：使用异步休眠
task<void> good_example(io_context& ctx) {
    co_await async_sleep(ctx, 1s);  // 不阻塞
}
```

### 4. 卸载 CPU 密集型工作

```cpp
task<int> cpu_intensive(io_context& ctx) {
    // 卸载到线程池
    co_return co_await blocking_invoke(ctx, [] {
        return expensive_computation();
    });
}
```

### 5. 优雅关闭

```cpp
class application {
    io_context ctx_;
    std::atomic<bool> shutdown_requested_{false};
    
    task<void> shutdown_monitor() {
        while (!shutdown_requested_) {
            co_await async_sleep(ctx_, 100ms);
        }
        ctx_.stop();
    }
    
public:
    void run() {
        spawn(ctx_, shutdown_monitor());
        ctx_.run();
    }
    
    void request_shutdown() {
        shutdown_requested_ = true;
    }
};
```

## 调试

### 启用日志

```cpp
// 设置日志级别
log::set_level(log::level::debug);

// 记录 I/O 操作
log::debug("Socket {} recv {} bytes", socket.fd(), n);
```

### 检查事件循环状态

```cpp
// 事件循环是否运行？
bool is_running = !ctx.stopped();

// 待处理操作数量
size_t pending = ctx.pending_count();
```

### 检测死锁

如果 `run()` 永不返回：
1. 检查是否所有任务都在等待 I/O
2. 验证定时器设置正确
3. 确保最终调用 `stop()`

## 常见陷阱

### 1. 忘记调用 `run()`

```cpp
// 错误：事件循环从未运行
int main() {
    io_context ctx;
    spawn(ctx, my_task(ctx));
    // 缺少：ctx.run();
}
```

### 2. 在协程中阻塞

```cpp
// 错误：阻塞事件循环
task<void> bad(io_context& ctx) {
    std::this_thread::sleep_for(1s);  // 阻塞所有任务！
}
```

### 3. 悬空引用

```cpp
// 错误：ctx 在任务完成前被销毁
void bad_function() {
    io_context ctx;
    spawn(ctx, long_running_task(ctx));
}  // ctx 被销毁，任务仍在运行！
```

## 下一步

- **[协程](coroutines.md)** - 理解 `task<T>` 和 `spawn()`
- **[同步原语](sync-primitives.md)** - Mutex、channel、semaphore
- **[TCP/UDP 指南](../protocols/tcp-udp.md)** - 网络编程基础
- **[多核架构](../advanced/multi-core.md)** - 扩展到多核
