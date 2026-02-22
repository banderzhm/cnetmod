/// cnetmod unit tests — async_shared_mutex read/write locking, guards, concurrency

#include "test_framework.hpp"

import cnetmod.coro.task;
import cnetmod.coro.shared_mutex;

using namespace cnetmod;

// =============================================================================
// Tests
// =============================================================================

TEST(rw_try_lock_free) {
    async_shared_mutex rw;
    ASSERT_TRUE(rw.try_lock());
    rw.unlock();
}

TEST(rw_try_lock_while_write_locked) {
    async_shared_mutex rw;
    ASSERT_TRUE(rw.try_lock());
    ASSERT_FALSE(rw.try_lock());  // Already write-locked
    rw.unlock();
}

TEST(rw_try_lock_after_write_unlock) {
    async_shared_mutex rw;
    ASSERT_TRUE(rw.try_lock());
    rw.unlock();
    ASSERT_TRUE(rw.try_lock());  // Free again
    rw.unlock();
}

TEST(rw_coro_write_lock_unlock) {
    async_shared_mutex rw;

    auto task_fn = [&]() -> task<int> {
        co_await rw.lock();
        int value = 42;
        rw.unlock();
        co_return value;
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 42);
}

TEST(rw_coro_read_lock_unlock) {
    async_shared_mutex rw;

    auto task_fn = [&]() -> task<int> {
        co_await rw.lock_shared();
        int value = 99;
        rw.unlock_shared();
        co_return value;
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 99);
}

TEST(rw_multiple_readers_sequential) {
    async_shared_mutex rw;
    int read_count = 0;

    auto reader = [&]() -> task<void> {
        co_await rw.lock_shared();
        ++read_count;
        rw.unlock_shared();
    };

    sync_wait(reader());
    sync_wait(reader());
    sync_wait(reader());

    ASSERT_EQ(read_count, 3);
}

TEST(rw_multiple_readers_concurrent) {
    // Two readers should both acquire shared lock simultaneously
    async_shared_mutex rw;
    int read_count = 0;

    auto reader = [&]() -> task<int> {
        co_await rw.lock_shared();
        ++read_count;
        rw.unlock_shared();
        co_return read_count;
    };

    auto [a, b] = sync_wait(when_all(reader(), reader()));
    ASSERT_EQ(read_count, 2);
}

TEST(rw_sequential_write) {
    async_shared_mutex rw;
    int counter = 0;

    auto writer = [&]() -> task<void> {
        co_await rw.lock();
        ++counter;
        rw.unlock();
    };

    sync_wait(writer());
    sync_wait(writer());
    sync_wait(writer());

    ASSERT_EQ(counter, 3);
}

TEST(rw_unique_lock_guard_basic) {
    async_shared_mutex rw;

    auto task_fn = [&]() -> task<int> {
        co_await rw.lock();
        async_unique_lock_guard guard(rw, std::adopt_lock);
        co_return 100;
        // guard unlocks here
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 100);

    // Should be unlocked now
    ASSERT_TRUE(rw.try_lock());
    rw.unlock();
}

TEST(rw_shared_lock_guard_basic) {
    async_shared_mutex rw;

    auto task_fn = [&]() -> task<int> {
        co_await rw.lock_shared();
        async_shared_lock_guard guard(rw, std::adopt_lock);
        co_return 200;
        // guard calls unlock_shared()
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 200);

    // Should be unlocked now
    ASSERT_TRUE(rw.try_lock());
    rw.unlock();
}

TEST(rw_unique_lock_guard_move) {
    async_shared_mutex rw;

    auto task_fn = [&]() -> task<void> {
        co_await rw.lock();
        async_unique_lock_guard guard(rw, std::adopt_lock);
        auto guard2 = std::move(guard);
        // guard released, guard2 holds lock
    };

    sync_wait(task_fn());
    ASSERT_TRUE(rw.try_lock());
    rw.unlock();
}

TEST(rw_shared_lock_guard_release) {
    async_shared_mutex rw;

    auto task_fn = [&]() -> task<void> {
        co_await rw.lock_shared();
        async_shared_lock_guard guard(rw, std::adopt_lock);
        guard.release();
        // guard won't unlock — must do manually
        rw.unlock_shared();
    };

    sync_wait(task_fn());
    ASSERT_TRUE(rw.try_lock());
    rw.unlock();
}

RUN_TESTS()
