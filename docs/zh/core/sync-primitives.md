# 同步原语

cnetmod 提供协程感知的同步原语，它们挂起而不是阻塞线程。

## 概述

所有原语都是：
- **协程感知**：挂起协程，而不是线程
- **零开销**：无线程阻塞或上下文切换
- **FIFO 排序**：为等待的协程提供公平调度
- **异常安全**：RAII 守卫用于自动清理

可用的原语：
- `mutex` - 独占锁
- `shared_mutex` - 读写锁
- `semaphore` - 计数信号量
- `channel<T>` - Go 风格的通信通道
- `wait_group` - 多任务屏障
- `cancel_token` - 协作取消

## Mutex

用于保护共享资源的独占锁。

### 基本用法

```cpp
import cnetmod;
using namespace cnetmod;

mutex mtx;
int shared_counter = 0;

task<void> increment(io_context& ctx) {
    for (int i = 0; i < 1000; ++i) {
        co_await mtx.lock();
        ++shared_counter;
        mtx.unlock();
    }
}
```

### RAII 守卫

```cpp
task<void> safe_increment(io_context& ctx) {
    auto guard = co_await mtx.scoped_lock();
    ++shared_counter;
    // guard 销毁时自动解锁
}
```

### Try Lock

```cpp
task<void> try_lock_example() {
    if (mtx.try_lock()) {
        // 获得了锁
        ++shared_counter;
        mtx.unlock();
    } else {
        // 锁不可用
        co_await handle_busy();
    }
}
```

### 实现细节

```cpp
class mutex {
    std::atomic<bool> locked_{false};
    std::mutex queue_mtx_;  // 保护等待者队列
    waiter_node* head_ = nullptr;
    waiter_node* tail_ = nullptr;
    
public:
    auto lock() -> lock_awaitable;
    void unlock();
    bool try_lock();
};
```

**FIFO 排序**：等待者排队并按顺序恢复。

## Shared Mutex

读写锁，允许多个读者或一个写者。

### 基本用法

```cpp
shared_mutex rw_mtx;
std::map<int, std::string> shared_data;

// 读者
task<std::string> read_data(int key) {
    co_await rw_mtx.lock_shared();
    auto value = shared_data[key];
    rw_mtx.unlock_shared();
    co_return value;
}

// 写者
task<void> write_data(int key, std::string value) {
    co_await rw_mtx.lock();
    shared_data[key] = std::move(value);
    rw_mtx.unlock();
}
```

### RAII 守卫

```cpp
// 读锁
task<std::string> read_with_guard(int key) {
    auto guard = co_await rw_mtx.scoped_lock_shared();
    co_return shared_data[key];
}

// 写锁
task<void> write_with_guard(int key, std::string value) {
    auto guard = co_await rw_mtx.scoped_lock();
    shared_data[key] = std::move(value);
}
```

### 写者优先

写者有优先权以防止写者饥饿：

```cpp
// 状态：5 个读者持有锁
// 写者到达 → 阻止新读者
// 现有读者完成 → 写者获得锁
// 新读者等待直到写者完成
```

### 实现

```cpp
class shared_mutex {
    std::mutex mtx_;
    int state_ = 0;  // 0=空闲，>0=N个读者，-1=写者
    
    waiter_node* write_head_ = nullptr;
    waiter_node* write_tail_ = nullptr;
    waiter_node* read_head_ = nullptr;
    waiter_node* read_tail_ = nullptr;
};
```

## Semaphore

用于资源限制的计数信号量。

### 基本用法

```cpp
semaphore sem(3);  // 允许 3 个并发操作

task<void> limited_operation() {
    co_await sem.acquire();
    
    // 只有 3 个任务可以同时在这里
    co_await do_work();
    
    sem.release();
}
```

### RAII 守卫

```cpp
task<void> safe_operation() {
    auto guard = co_await sem.scoped_acquire();
    co_await do_work();
    // 自动释放
}
```

### Try Acquire

```cpp
task<void> try_acquire_example() {
    if (sem.try_acquire()) {
        co_await do_work();
        sem.release();
    } else {
        // 资源不可用
        co_await handle_busy();
    }
}
```

### 使用场景

**连接池**：
```cpp
class connection_pool {
    semaphore sem_;
    std::vector<connection> connections_;
    
public:
    connection_pool(size_t size) : sem_(size) {
        for (size_t i = 0; i < size; ++i)
            connections_.push_back(create_connection());
    }
    
    task<connection> acquire() {
        co_await sem_.acquire();
        co_return get_available_connection();
    }
    
    void release(connection conn) {
        return_connection(conn);
        sem_.release();
    }
};
```

**速率限制**：
```cpp
class rate_limiter {
    semaphore sem_;
    io_context& ctx_;
    
public:
    rate_limiter(io_context& ctx, int rate_per_second)
        : sem_(rate_per_second), ctx_(ctx) {
        spawn(ctx_, refill_loop());
    }
    
    task<void> acquire() {
        co_await sem_.acquire();
    }
    
private:
    task<void> refill_loop() {
        while (true) {
            co_await async_sleep(ctx_, 1s);
            sem_.release(rate_per_second_);
        }
    }
};
```

## Channel

协程间通信的 Go 风格通道。

### 基本用法

```cpp
channel<int> ch(10);  // 缓冲通道，容量 10

// 生产者
task<void> producer() {
    for (int i = 0; i < 100; ++i) {
        co_await ch.send(i);
    }
    ch.close();
}

// 消费者
task<void> consumer() {
    while (true) {
        auto value = co_await ch.recv();
        if (!value) break;  // 通道关闭
        std::println("Received: {}", *value);
    }
}
```

### 无缓冲通道

```cpp
channel<int> ch(0);  // 无缓冲（同步）

// send() 阻塞直到 recv() 被调用
// recv() 阻塞直到 send() 被调用
```

### Select 模式（手动）

```cpp
task<void> select_example(channel<int>& ch1, channel<int>& ch2) {
    // 首先尝试 ch1
    if (auto val = ch1.try_recv()) {
        std::println("ch1: {}", *val);
        co_return;
    }
    
    // 尝试 ch2
    if (auto val = ch2.try_recv()) {
        std::println("ch2: {}", *val);
        co_return;
    }
    
    // 两者都为空，等待第一个可用的
    // （需要自定义 select 实现）
}
```

### 使用场景

**生产者-消费者**：
```cpp
task<void> pipeline(io_context& ctx) {
    channel<std::string> ch(100);
    
    // 生产者
    spawn(ctx, [&]() -> task<void> {
        for (int i = 0; i < 1000; ++i) {
            co_await ch.send(std::format("item-{}", i));
        }
        ch.close();
    }());
    
    // 消费者
    spawn(ctx, [&]() -> task<void> {
        while (auto item = co_await ch.recv()) {
            co_await process(*item);
        }
    }());
}
```

**扇出**：
```cpp
task<void> fan_out(io_context& ctx) {
    channel<int> input(10);
    channel<int> output1(10);
    channel<int> output2(10);
    
    // 分发器
    spawn(ctx, [&]() -> task<void> {
        while (auto val = co_await input.recv()) {
            co_await output1.send(*val);
            co_await output2.send(*val);
        }
        output1.close();
        output2.close();
    }());
}
```

**扇入**：
```cpp
task<void> fan_in(io_context& ctx) {
    channel<int> input1(10);
    channel<int> input2(10);
    channel<int> output(10);
    
    // 合并器
    spawn(ctx, [&]() -> task<void> {
        while (true) {
            if (auto val = co_await input1.recv()) {
                co_await output.send(*val);
            } else if (auto val = co_await input2.recv()) {
                co_await output.send(*val);
            } else {
                break;  // 两者都关闭
            }
        }
        output.close();
    }());
}
```

## Wait Group

等待多个任务的屏障。

### 基本用法

```cpp
wait_group wg;

task<void> parallel_work(io_context& ctx) {
    wg.add(3);  // 期望 3 个任务
    
    spawn(ctx, [&]() -> task<void> {
        co_await task1();
        wg.done();
    }());
    
    spawn(ctx, [&]() -> task<void> {
        co_await task2();
        wg.done();
    }());
    
    spawn(ctx, [&]() -> task<void> {
        co_await task3();
        wg.done();
    }());
    
    co_await wg.wait();  // 等待所有 3 个任务
    std::println("All tasks complete");
}
```

### 动态任务计数

```cpp
task<void> dynamic_tasks(io_context& ctx) {
    wait_group wg;
    
    for (int i = 0; i < 10; ++i) {
        wg.add(1);
        spawn(ctx, [&, i]() -> task<void> {
            co_await process_item(i);
            wg.done();
        }());
    }
    
    co_await wg.wait();
}
```

### 使用场景

**并行请求**：
```cpp
task<std::vector<response>> fetch_all(
    io_context& ctx,
    const std::vector<std::string>& urls
) {
    std::vector<response> results(urls.size());
    wait_group wg;
    
    for (size_t i = 0; i < urls.size(); ++i) {
        wg.add(1);
        spawn(ctx, [&, i]() -> task<void> {
            results[i] = co_await http_get(urls[i]);
            wg.done();
        }());
    }
    
    co_await wg.wait();
    co_return results;
}
```

## Cancel Token

协作取消机制。

### 基本用法

```cpp
cancel_token token;

task<void> cancellable_work(cancel_token token) {
    while (!token.is_cancelled()) {
        co_await do_work();
    }
    std::println("Work cancelled");
}

// 启动工作
spawn(ctx, cancellable_work(token));

// 稍后：取消
token.cancel();
```

### 取消回调

```cpp
task<void> with_cleanup(cancel_token token) {
    token.on_cancel([] {
        std::println("Cleaning up...");
    });
    
    while (!token.is_cancelled()) {
        co_await do_work();
    }
}
```

### 带取消的超时

```cpp
task<void> timeout_example(io_context& ctx) {
    cancel_token token;
    
    // 启动工作
    spawn(ctx, long_running_task(token));
    
    // 超时后取消
    co_await async_sleep(ctx, 5s);
    token.cancel();
}
```

### 分层取消

```cpp
task<void> parent_task(cancel_token parent_token) {
    cancel_token child_token;
    
    // 父级取消时取消子级
    parent_token.on_cancel([&] {
        child_token.cancel();
    });
    
    spawn(ctx, child_task(child_token));
}
```

## 性能比较

| 原语 | 获取成本 | 释放成本 | 内存 |
|-----------|--------------|--------------|--------|
| `mutex` | 约 50 ns | 约 30 ns | 64 字节 |
| `shared_mutex` | 约 60 ns（读） | 约 40 ns | 128 字节 |
| `semaphore` | 约 50 ns | 约 30 ns | 64 字节 |
| `channel<T>` | 约 100 ns | 约 100 ns | 128 + 缓冲区 |
| `wait_group` | 约 20 ns（add） | 约 30 ns（done） | 32 字节 |

## 最佳实践

### 1. 使用 RAII 守卫

```cpp
// 好：自动解锁
auto guard = co_await mtx.scoped_lock();

// 不好：手动解锁（容易出错）
co_await mtx.lock();
// ... 异常可能跳过解锁 ...
mtx.unlock();
```

### 2. 最小化临界区

```cpp
// 好：短临界区
{
    auto guard = co_await mtx.scoped_lock();
    ++counter;
}
co_await expensive_operation();  // 锁外

// 不好：长临界区
auto guard = co_await mtx.scoped_lock();
++counter;
co_await expensive_operation();  // 持有锁！
```

### 3. 避免死锁

```cpp
// 不好：潜在死锁
task<void> task1() {
    co_await mtx1.lock();
    co_await mtx2.lock();  // 如果 task2 持有 mtx2 则死锁
}

task<void> task2() {
    co_await mtx2.lock();
    co_await mtx1.lock();  // 如果 task1 持有 mtx1 则死锁
}

// 好：一致的锁顺序
task<void> task1() {
    co_await mtx1.lock();
    co_await mtx2.lock();
}

task<void> task2() {
    co_await mtx1.lock();  // 相同顺序
    co_await mtx2.lock();
}
```

### 4. 使用 Channel 进行通信

```cpp
// 好：基于通道的通信
channel<message> ch(10);
spawn(ctx, producer(ch));
spawn(ctx, consumer(ch));

// 不好：带互斥锁的共享状态
mutex mtx;
std::queue<message> queue;
// 更复杂，容易出错
```

### 5. 优先使用 `wait_group` 而不是手动计数

```cpp
// 好：wait_group
wait_group wg;
wg.add(N);
// ... 生成任务 ...
co_await wg.wait();

// 不好：手动计数
std::atomic<int> counter{N};
// ... 生成递减的任务 ...
while (counter > 0) {
    co_await async_sleep(ctx, 10ms);  // 轮询！
}
```

## 下一步

- **[协程](coroutines.md)** - 理解 `task<T>` 和 async/await
- **[I/O 上下文](io-context.md)** - 事件循环和调度
- **[TCP/UDP 指南](../protocols/tcp-udp.md)** - 网络编程
- **[HTTP 服务器](../protocols/http.md)** - 构建 Web 服务
