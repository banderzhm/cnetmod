# Mutex and Lock Usage Guide

Comprehensive guide to using mutexes and locks in cnetmod for thread-safe coroutine programming.

## Overview

cnetmod provides coroutine-aware synchronization primitives:
- **`mutex`**: Exclusive lock (one owner at a time)
- **`shared_mutex`**: Reader-writer lock (multiple readers OR one writer)
- **`semaphore`**: Counting semaphore (N concurrent owners)

All primitives **suspend the coroutine** instead of blocking the thread.

## Mutex Basics

### Simple Lock/Unlock

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

### RAII Lock Guard (Recommended)

```cpp
task<void> increment_safe(io_context& ctx) {
    // Acquire lock with RAII guard
    auto guard = co_await mtx.scoped_lock();
    
    // Critical section
    ++shared_counter;
    
    // Lock automatically released when guard destroyed
}
```

### Try Lock (Non-Blocking)

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

## Shared Mutex (Reader-Writer Lock)

### Read Lock (Shared)

Multiple readers can hold the lock simultaneously:

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

### Write Lock (Exclusive)

Only one writer can hold the lock:

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

### Writer Priority

Writers have priority to prevent writer starvation:

```cpp
// Scenario:
// 1. 5 readers holding lock
// 2. Writer arrives → blocks new readers
// 3. Existing readers finish
// 4. Writer gets lock
// 5. New readers wait until writer finishes
```

## Common Patterns

### Protecting Shared State

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

### Cache with Read-Write Lock

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

### Double-Checked Locking

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

### Lock Multiple Mutexes

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

## Deadlock Prevention

### Rule 1: Consistent Lock Ordering

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
    mtx_a.unlock();
}
```

### Rule 2: Use try_lock

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

### Rule 3: Use Timeout

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

## Performance Optimization

### Minimize Critical Sections

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

### Use Read-Write Lock Appropriately

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

### Lock-Free Alternatives

For simple counters, consider atomic operations:

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

## Advanced Patterns

### Condition Variable Pattern

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

### Scoped Lock with Custom Action

```cpp
template<typename Mutex, typename Action>
class scoped_lock_with_action {
    Mutex& mtx_;
    Action action_;
    
public:
    scoped_lock_with_action(Mutex& mtx, Action action)
        : mtx_(mtx), action_(std::move(action)) {}
    
    ~scoped_lock_with_action() {
        action_();
        mtx_.unlock();
    }
};

// Usage
task<void> example() {
    co_await mtx.lock();
    scoped_lock_with_action guard(mtx, [] {
        std::println("Lock released");
    });
    
    // Critical section
}
```

### Recursive Lock (Not Recommended)

```cpp
// cnetmod doesn't provide recursive_mutex
// If you need recursion, refactor your code

// BAD: Recursive locking
task<void> recursive_bad() {
    co_await mtx.lock();
    co_await helper();  // Also tries to lock mtx → deadlock!
    mtx.unlock();
}

task<void> helper() {
    co_await mtx.lock();  // Deadlock!
    // ...
    mtx.unlock();
}

// GOOD: Pass lock state
task<void> recursive_good() {
    auto guard = co_await mtx.scoped_lock();
    co_await helper_unlocked();
}

task<void> helper_unlocked() {
    // Assumes caller holds lock
    // ...
}
```

## Debugging Lock Issues

### Detect Deadlocks

```cpp
// Add timeout to all locks in debug builds
#ifdef DEBUG
template<typename Mutex>
task<void> debug_lock(Mutex& mtx, io_context& ctx) {
    try {
        co_await with_timeout(ctx, mtx.lock(), 5s);
    } catch (const timeout_error&) {
        std::println("DEADLOCK DETECTED at {}", std::source_location::current());
        std::abort();
    }
}
#define LOCK(mtx) debug_lock(mtx, ctx)
#else
#define LOCK(mtx) mtx.lock()
#endif
```

### Lock Ordering Validation

```cpp
class lock_order_checker {
    static thread_local std::vector<void*> held_locks_;
    
public:
    static void acquire(void* lock) {
        // Check if we already hold a "later" lock
        for (auto* held : held_locks_) {
            if (held > lock) {
                std::println("Lock ordering violation!");
                std::abort();
            }
        }
        held_locks_.push_back(lock);
    }
    
    static void release(void* lock) {
        std::erase(held_locks_, lock);
    }
};
```

## Best Practices

1. **Always use RAII guards**: Prevents forgetting to unlock
2. **Keep critical sections short**: Minimize lock hold time
3. **Use shared_mutex for read-heavy workloads**: Better concurrency
4. **Consistent lock ordering**: Prevents deadlocks
5. **Avoid nested locks**: Simplifies reasoning
6. **Use channel<T> for communication**: Better than mutex + queue
7. **Document lock invariants**: What does each lock protect?
8. **Test with thread sanitizer**: Detect data races

## Next Steps

- **[Synchronization Primitives](../core/sync-primitives.md)** - Overview of all primitives
- **[Coroutines](../core/coroutines.md)** - Understand async/await
- **[Channel Guide](channel-guide.md)** - Alternative to mutex + queue
- **[Performance](performance.md)** - Optimize lock usage
