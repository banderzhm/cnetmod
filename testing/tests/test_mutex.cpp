/// cnetmod unit tests — async_mutex lock/unlock, try_lock, lock_guard

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;

using namespace cnetmod;

// =============================================================================
// Tests
// =============================================================================

TEST(mutex_try_lock_unlocked) {
    async_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_try_lock_locked) {
    async_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    ASSERT_FALSE(mtx.try_lock());  // Already locked
    mtx.unlock();
}

TEST(mutex_try_lock_after_unlock) {
    async_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
    ASSERT_TRUE(mtx.try_lock());  // Should succeed again
    mtx.unlock();
}

TEST(mutex_coro_lock_unlock) {
    async_mutex mtx;

    auto task_fn = [&]() -> task<int> {
        co_await mtx.lock();
        // In critical section
        int value = 42;
        mtx.unlock();
        co_return value;
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 42);
}

TEST(mutex_lock_guard_basic) {
    async_mutex mtx;

    auto task_fn = [&]() -> task<int> {
        co_await mtx.lock();
        async_lock_guard guard(mtx, std::adopt_lock);
        co_return 100;
        // guard unlocks here
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 100);

    // Mutex should be unlocked after guard destructor
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_lock_guard_move) {
    async_mutex mtx;

    auto task_fn = [&]() -> task<void> {
        co_await mtx.lock();
        async_lock_guard guard(mtx, std::adopt_lock);

        // Move guard
        async_lock_guard guard2 = std::move(guard);
        // guard is now released, guard2 holds the lock
    };

    sync_wait(task_fn());

    // Mutex should be unlocked
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_lock_guard_release) {
    async_mutex mtx;

    auto task_fn = [&]() -> task<void> {
        co_await mtx.lock();
        async_lock_guard guard(mtx, std::adopt_lock);
        guard.release();
        // guard won't unlock on destruction
        // Must unlock manually
        mtx.unlock();
    };

    sync_wait(task_fn());
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_sequential_lock) {
    async_mutex mtx;
    int counter = 0;

    auto increment = [&]() -> task<void> {
        co_await mtx.lock();
        async_lock_guard guard(mtx, std::adopt_lock);
        ++counter;
    };

    // Sequential — each lock/unlock should succeed
    sync_wait(increment());
    sync_wait(increment());
    sync_wait(increment());

    ASSERT_EQ(counter, 3);
}

// =============================================================================
// Extended Tests
// =============================================================================

TEST(mutex_when_all_contention) {
    // Two coroutines competing for the same mutex via when_all
    async_mutex mtx;
    int counter = 0;

    auto increment = [&]() -> task<int> {
        co_await mtx.lock();
        async_lock_guard guard(mtx, std::adopt_lock);
        ++counter;
        co_return counter;
    };

    auto [a, b] = sync_wait(when_all(increment(), increment()));
    ASSERT_EQ(counter, 2);
    // One should be 1 and the other 2
    ASSERT_TRUE((a == 1 && b == 2) || (a == 2 && b == 1));
}

TEST(mutex_lock_guard_exception_safety) {
    async_mutex mtx;

    auto task_fn = [&]() -> task<void> {
        co_await mtx.lock();
        async_lock_guard guard(mtx, std::adopt_lock);
        throw std::runtime_error("test");
        // guard should still unlock even though exception is thrown
    };

    bool caught = false;
    try {
        sync_wait(task_fn());
    } catch (const std::runtime_error&) {
        caught = true;
    }
    ASSERT_TRUE(caught);

    // Mutex should be unlocked (guard destructor ran during stack unwinding)
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_many_sequential_cycles) {
    async_mutex mtx;
    int counter = 0;

    auto increment = [&]() -> task<void> {
        co_await mtx.lock();
        ++counter;
        mtx.unlock();
    };

    for (int i = 0; i < 100; ++i)
        sync_wait(increment());

    ASSERT_EQ(counter, 100);
}

RUN_TESTS()
