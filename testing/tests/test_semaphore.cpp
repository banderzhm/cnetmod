/// cnetmod unit tests — async_semaphore acquire/release, try_acquire, counting

#include "test_framework.hpp"

import cnetmod.coro.task;
import cnetmod.coro.semaphore;

using namespace cnetmod;

// =============================================================================
// Tests
// =============================================================================

TEST(sem_initial_count) {
    async_semaphore sem(5);
    ASSERT_EQ(sem.available(), (std::size_t)5);
}

TEST(sem_try_acquire_available) {
    async_semaphore sem(3);
    ASSERT_TRUE(sem.try_acquire());
    ASSERT_EQ(sem.available(), (std::size_t)2);
}

TEST(sem_try_acquire_exhausted) {
    async_semaphore sem(1);
    ASSERT_TRUE(sem.try_acquire());
    ASSERT_FALSE(sem.try_acquire());  // No permits left
    ASSERT_EQ(sem.available(), (std::size_t)0);
}

TEST(sem_release_increases_count) {
    async_semaphore sem(0);
    ASSERT_EQ(sem.available(), (std::size_t)0);
    sem.release();
    ASSERT_EQ(sem.available(), (std::size_t)1);
    sem.release();
    ASSERT_EQ(sem.available(), (std::size_t)2);
}

TEST(sem_acquire_then_release) {
    async_semaphore sem(1);

    auto task_fn = [&]() -> task<int> {
        co_await sem.acquire();
        int value = 42;
        sem.release();
        co_return value;
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 42);
    ASSERT_EQ(sem.available(), (std::size_t)1);
}

TEST(sem_multiple_acquire_release) {
    async_semaphore sem(3);

    // Acquire all 3
    ASSERT_TRUE(sem.try_acquire());
    ASSERT_TRUE(sem.try_acquire());
    ASSERT_TRUE(sem.try_acquire());
    ASSERT_FALSE(sem.try_acquire());
    ASSERT_EQ(sem.available(), (std::size_t)0);

    // Release all 3
    sem.release(3);
    ASSERT_EQ(sem.available(), (std::size_t)3);
}

TEST(sem_coro_acquire_immediate) {
    // Semaphore with count > 0 should return immediately on co_await acquire()
    async_semaphore sem(2);

    auto task_fn = [&]() -> task<void> {
        co_await sem.acquire();
        co_await sem.acquire();
        // Both should succeed immediately
        sem.release(2);
    };
    sync_wait(task_fn());
    ASSERT_EQ(sem.available(), (std::size_t)2);
}

TEST(sem_sequential_contention) {
    // Simulate sequential access pattern
    async_semaphore sem(1);
    int counter = 0;

    auto increment = [&]() -> task<void> {
        co_await sem.acquire();
        ++counter;
        sem.release();
    };

    sync_wait(increment());
    sync_wait(increment());
    sync_wait(increment());

    ASSERT_EQ(counter, 3);
    ASSERT_EQ(sem.available(), (std::size_t)1);
}

TEST(sem_concurrent_access_when_all) {
    // Two coroutines competing for a semaphore with count=2
    // Both should acquire immediately (no contention)
    async_semaphore sem(2);
    int counter = 0;

    auto work = [&]() -> task<int> {
        co_await sem.acquire();
        ++counter;
        sem.release();
        co_return counter;
    };

    auto [a, b] = sync_wait(when_all(work(), work()));
    // Both should complete
    ASSERT_EQ(counter, 2);
}

RUN_TESTS()
