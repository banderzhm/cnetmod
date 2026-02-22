# 互斥锁和锁使用指南

在 cnetmod 中使用互斥锁和锁进行线程安全协程编程的综合指南。

## 概述

cnetmod 提供协程感知的同步原语：
- **`mutex`**：独占锁（一次一个所有者）
- **`shared_mutex`**：读写锁（多个读者或一个写者）
- **`semaphore`**：计数信号量（N 个并发所有者）

所有原语都**挂起协程**而不是阻塞线程。

## 互斥锁基础

### 简单锁定/解锁

```cpp
import cnetmod;
using namespace cnetmod;

mutex mtx;
int shared_counter = 0;

task<void> increment(io_context& ctx) {
    // Acquire lock
    co_await mtx.lock();
    
    // Critical section
    ++shared_counter;
    
    // Release lock
    mtx.unlock();
}
```

### RAII 锁保护（推荐）

```cpp
task<void> increment_safe(io_context& ctx) {
    // Acquire lock with RAII guard
    auto guard = co_await mtx.scoped_lock();
    
    // Critical section
    ++shared_counter;
    
    // Lock automatically released when guard destroyed
}
```

### 尝试锁定（非阻塞）

```cpp
task<void> try_increment(io_context& ctx) {
    if (mtx.try_lock()) {
        // Got the lock immediately
        ++shared_counter;
        mtx.unlock();
    } else {
        // Lock not available, do something else
        co_await handle_busy();
    }
}
```

## 共享互斥锁（读写锁）

### 读锁（共享）

多个读者可以同时持有锁：

```cpp
shared_mutex rw_mtx;
std::map<int, std::string> shared_data;

task<std::string> read_data(int key) {
    // Acquire read lock
    co_await rw_mtx.lock_shared();
    
    // Read operation (multiple readers allowed)
    auto value = shared_data[key];
    
    // Release read lock
    rw_mtx.unlock_shared();
    
    co_return value;
}

// Or with RAII guard
task<std::string> read_data_safe(int key) {
    auto guard = co_await rw_mtx.scoped_lock_shared();
    co_return shared_data[key];
}
```

### 写锁（独占）

只有一个写者可以持有锁：

```cpp
task<void> write_data(int key, std::string value) {
    // Acquire write lock (exclusive)
    co_await rw_mtx.lock();
    
    // Write operation (no other readers or writers)
    shared_data[key] = std::move(value);
    
    // Release write lock
    rw_mtx.unlock();
}

// Or with RAII guard
task<void> write_data_safe(int key, std::string value) {
    auto guard = co_await rw_mtx.scoped_lock();
    shared_data[key] = std::move(value);
}
```

### 写者优先

写者具有优先权以防止写者饥饿：

```cpp
// Scenario:
// 1. 5 readers holding lock
// 2. Writer arrives → blocks new readers
// 3. Existing readers finish
// 4. Writer gets lock
// 5. New readers wait until writer finishes
```

## 常见模式

### 保护共享状态

```cpp
class thread_safe_counter {
    mutex mtx_;
    int value_ = 0;
    
public:
    task<void> increment() {
        auto guard = co_await mtx_.scoped_lock();
        ++value_;
    }
    
    task<int> get() {
        auto guard = co_await mtx_.scoped_lock();
        co_return value_;
    }
};
```

### 使用读写锁的缓存

```cpp
class cache {
    shared_mutex mtx_;
    std::unordered_map<std::string, std::string> data_;
    
public:
    // Read (shared lock)
    task<std::optional<std::string>> get(std::string_view key) {
        auto guard = co_await mtx_.scoped_lock_shared();
        
        auto it = data_.find(std::string(key));
        if (it != data_.end()) {
            co_return it->second;
        }
        co_return std::nullopt;
    }
    
    // Write (exclusive lock)
    task<void> set(std::string key, std::string value) {
        auto guard = co_await mtx_.scoped_lock();
        data_[std::move(key)] = std::move(value);
    }
    
    // Delete (exclusive lock)
    task<void> remove(std::string_view key) {
        auto guard = co_await mtx_.scoped_lock();
        data_.erase(std::string(key));
    }
};
```

### 双重检查锁定

```cpp
class lazy_init {
    shared_mutex mtx_;
    std::unique_ptr<resource> res_;
    
public:
    task<resource*> get() {
        // First check (read lock)
        {
            auto guard = co_await mtx_.scoped_lock_shared();
            if (res_) {
                co_return res_.get();
            }
        }
        
        // Second check (write lock)
        {
            auto guard = co_await mtx_.scoped_lock();
            if (!res_) {
                res_ = std::make_unique<resource>();
            }
            co_return res_.get();
        }
    }
};
```

### 锁定多个互斥锁

```cpp
task<void> transfer(account& from, account& to, int amount) {
    // Lock both accounts (always in same order to avoid deadlock)
    auto& first = (from.id() < to.id()) ? from : to;
    auto& second = (from.id() < to.id()) ? to : from;
    
    auto guard1 = co_await first.lock();
    auto guard2 = co_await second.lock();
    
    // Both locked, perform transfer
    from.withdraw(amount);
    to.deposit(amount);
}
```

## 死锁预防

### 规则 1：一致的锁定顺序

```cpp
// BAD: Potential deadlock
task<void> task1() {
    co_await mtx_a.lock();
    co_await mtx_b.lock();  // Deadlock if task2 holds mtx_b
    // ...
    mtx_b.unlock();
    mtx_a.unlock();
}

task<void> task2() {
    co_await mtx_b.lock();
    co_await mtx_a.lock();  // Deadlock if task1 holds mtx_a
    // ...
    mtx_a.unlock();
    mtx_b.unlock();
}

// GOOD: Consistent ordering
task<void> task1() {
    co_await mtx_a.lock();  // Always lock A first
    co_await mtx_b.lock();
    // ...
    mtx_b.unlock();
    mtx_a.unlock();
}

task<void> task2() {
    co_await mtx_a.lock();  // Always lock A first
    co_await mtx_b.lock();
    // ...
    mtx_b.unlock();
    mtx_b.unlock();
}
```

### 规则 2：使用 try_lock

```cpp
task<void> safe_lock_multiple() {
    while (true) {
        co_await mtx_a.lock();
        
        if (mtx_b.try_lock()) {
            // Got both locks
            break;
        }
        
        // Couldn't get mtx_b, release mtx_a and retry
        mtx_a.unlock();
        co_await async_sleep(ctx, 10ms);
    }
    
    // Both locked
    // ...
    
    mtx_b.unlock();
    mtx_a.unlock();
}
```

### 规则 3：使用超时

```cpp
task<void> lock_with_timeout(io_context& ctx) {
    try {
        co_await with_timeout(ctx, mtx.lock(), 5s);
        
        // Got lock
        // ...
        
        mtx.unlock();
    } catch (const timeout_error&) {
        // Timeout, possible deadlock
        std::println("Lock timeout, possible deadlock");
    }
}
```

## 性能优化

### 最小化临界区

```cpp
// BAD: Long critical section
task<void> bad_example() {
    auto guard = co_await mtx.scoped_lock();
    
    auto data = fetch_data();  // Slow!
    process_data(data);        // Slow!
    shared_state = data;
}

// GOOD: Short critical section
task<void> good_example() {
    auto data = fetch_data();  // Outside lock
    process_data(data);        // Outside lock
    
    {
        auto guard = co_await mtx.scoped_lock();
        shared_state = data;   // Only this needs lock
    }
}
```

### 适当使用读写锁

```cpp
// If reads >> writes, use shared_mutex
class read_heavy_cache {
    shared_mutex mtx_;
    std::map<int, std::string> data_;
    
public:
    // Frequent reads (shared lock)
    task<std::string> get(int key) {
        auto guard = co_await mtx_.scoped_lock_shared();
        co_return data_[key];
    }
    
    // Rare writes (exclusive lock)
    task<void> set(int key, std::string value) {
        auto guard = co_await mtx_.scoped_lock();
        data_[key] = std::move(value);
    }
};
```

### 无锁替代方案

对于简单计数器，考虑使用原子操作：

```cpp
// Instead of mutex-protected counter
class counter {
    mutex mtx_;
    int value_ = 0;
    
public:
    task<void> increment() {
        auto guard = co_await mtx_.scoped_lock();
        ++value_;
    }
};

// Use atomic
class atomic_counter {
    std::atomic<int> value_{0};
    
public:
    void increment() {
        value_.fetch_add(1, std::memory_order_relaxed);
    }
};
```

## 高级模式

### 条件变量模式

```cpp
class queue {
    mutex mtx_;
    std::queue<int> data_;
    
public:
    task<void> push(int value) {
        auto guard = co_await mtx_.scoped_lock();
        data_.push(value);
    }
    
    task<int> pop(io_context& ctx) {
        while (true) {
            {
                auto guard = co_await mtx_.scoped_lock();
                if (!data_.empty()) {
                    int value = data_.front();
                    data_.pop();
                    co_return value;
                }
            }
            
            // Wait before retry
            co_await async_sleep(ctx, 10ms);
        }
    }
};

// Better: Use channel<T> instead
channel<int> ch(10);
co_await ch.send(42);
auto value = co_await ch.recv();
```

## 最佳实践

1. **始终使用 RAII 保护**：防止忘记解锁
2. **保持临界区简短**：最小化锁持有时间
3. **对读密集型工作负载使用 shared_mutex**：更好的并发性
4. **一致的锁定顺序**：防止死锁
5. **避免嵌套锁**：简化推理
6. **使用 channel<T> 进行通信**：优于 mutex + queue
7. **记录锁不变量**：每个锁保护什么？
8. **使用线程清理器测试**：检测数据竞争

## 下一步

- **[同步原语](../core/sync-primitives.md)** - 所有原语概述
- **[协程](../core/coroutines.md)** - 理解 async/await
- **[Channel 指南](channel-guide.md)** - mutex + queue 的替代方案
- **[性能](performance.md)** - 优化锁使用
