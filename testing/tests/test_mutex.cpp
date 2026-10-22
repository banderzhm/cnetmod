/// cnetmod unit tests — async_mutex lock/unlock, try_lock, lock_guard

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.mutex;
import cnetmod.coro.striped_mutex;
import cnetmod.io.io_context;

using namespace cnetmod;

// =============================================================================
// Tests
// =============================================================================

TEST(mutex_try_lock_unlocked)
{
    async_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_try_lock_locked)
{
    async_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    ASSERT_FALSE(mtx.try_lock()); // Already locked
    mtx.unlock();
}

TEST(mutex_try_lock_after_unlock)
{
    async_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
    ASSERT_TRUE(mtx.try_lock()); // Should succeed again
    mtx.unlock();
}

TEST(mutex_coro_lock_unlock)
{
    async_mutex mtx;

    auto task_fn = [&]() -> task<int>
    {
        co_await mtx.lock();
        // In critical section
        int value = 42;
        mtx.unlock();
        co_return value;
    };

    auto result = sync_wait(task_fn());
    ASSERT_EQ(result, 42);
}

TEST(mutex_lock_guard_basic)
{
    async_mutex mtx;

    auto task_fn = [&]() -> task<int>
    {
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

TEST(mutex_lock_guard_move)
{
    async_mutex mtx;

    auto task_fn = [&]() -> task<void>
    {
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

TEST(mutex_lock_guard_release)
{
    async_mutex mtx;

    auto task_fn = [&]() -> task<void>
    {
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

TEST(mutex_sequential_lock)
{
    async_mutex mtx;
    int counter = 0;

    auto increment = [&]() -> task<void>
    {
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

TEST(mutex_when_all_contention)
{
    // Two coroutines competing for the same mutex via when_all
    async_mutex mtx;
    int counter = 0;

    auto increment = [&]() -> task<int>
    {
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

TEST(striped_async_mutex_serializes_the_same_business_key)
{
    striped_async_mutex<std::string> gates{16};
    int counter = 0;

    auto increment = [&]() -> task<int>
    {
        auto guard = co_await gates.lock("user:42");
        co_return ++counter;
    };

    sync_wait(when_all(increment(), increment(), increment()));
    ASSERT_EQ(counter, 3);
}

TEST(mutex_lock_guard_exception_safety)
{
    async_mutex mtx;

    auto task_fn = [&]() -> task<void>
    {
        co_await mtx.lock();
        async_lock_guard guard(mtx, std::adopt_lock);
        throw std::runtime_error("test");
        // guard should still unlock even though exception is thrown
    };

    bool caught = false;
    try
    {
        sync_wait(task_fn());
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    ASSERT_TRUE(caught);

    // Mutex should be unlocked (guard destructor ran during stack unwinding)
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_many_sequential_cycles)
{
    async_mutex mtx;
    int counter = 0;

    auto increment = [&]() -> task<void>
    {
        co_await mtx.lock();
        ++counter;
        mtx.unlock();
    };

    for (int i = 0; i < 100; ++i)
        sync_wait(increment());

    ASSERT_EQ(counter, 100);
}

TEST(mutex_try_lock_is_exclusive_across_threads)
{
    async_mutex mtx;
    constexpr std::size_t thread_count = 8;
    constexpr std::size_t iterations_per_thread = 25'000;
    std::atomic<std::size_t> entries{0};
    std::atomic<std::size_t> inside{0};
    std::atomic<std::size_t> overlap_count{0};
    std::barrier start_line{
        static_cast<std::ptrdiff_t>(thread_count)};
    std::vector<std::jthread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread = 0; thread < thread_count; ++thread)
    {
        threads.emplace_back([&]
        {
            start_line.arrive_and_wait();
            for (std::size_t iteration = 0;
                 iteration < iterations_per_thread; ++iteration)
            {
                while (!mtx.try_lock())
                    std::this_thread::yield();

                if (inside.fetch_add(1, std::memory_order_acq_rel) != 0)
                    overlap_count.fetch_add(1, std::memory_order_relaxed);
                entries.fetch_add(1, std::memory_order_relaxed);
                inside.fetch_sub(1, std::memory_order_acq_rel);
                mtx.unlock();
            }
        });
    }
    threads.clear();

    ASSERT_EQ(overlap_count.load(std::memory_order_acquire), 0U);
    ASSERT_EQ(inside.load(std::memory_order_acquire), 0U);
    ASSERT_EQ(entries.load(std::memory_order_acquire),
        thread_count * iterations_per_thread);
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(mutex_hands_off_all_waiters_across_event_loops)
{
    async_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());

    auto first = make_io_context();
    auto second = make_io_context();
    auto third = make_io_context();
    auto fourth = make_io_context();
    std::array<io_context*, 4> loops{
        first.get(), second.get(), third.get(), fourth.get()};
    constexpr std::size_t waiters_per_loop = 32;
    constexpr std::size_t waiter_count = loops.size() * waiters_per_loop;
    std::atomic<std::size_t> awaiting{0};
    std::atomic<std::size_t> completed{0};
    std::atomic<std::size_t> inside{0};
    std::atomic<std::size_t> overlap_count{0};
    std::atomic<std::size_t> wrong_loop_count{0};

    auto waiter = [&](io_context* owner) -> task<void>
    {
        // Publishing before co_await deliberately lets the releasing thread
        // race both await_ready() and await_suspend(). Every interleaving must
        // either acquire immediately or enqueue exactly once.
        awaiting.fetch_add(1, std::memory_order_release);
        co_await mtx.lock();

        if (io_context::current() != owner)
            wrong_loop_count.fetch_add(1, std::memory_order_relaxed);
        if (inside.fetch_add(1, std::memory_order_acq_rel) != 0)
            overlap_count.fetch_add(1, std::memory_order_relaxed);
        inside.fetch_sub(1, std::memory_order_acq_rel);
        mtx.unlock();

        if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 ==
            waiter_count)
        {
            for (auto* loop : loops)
                loop->stop();
        }
    };

    for (auto* loop : loops)
    {
        for (std::size_t index = 0; index < waiters_per_loop; ++index)
            spawn(*loop, waiter(loop));
    }

    std::array<std::jthread, 4> runners{
        std::jthread{[&] { first->run(); }},
        std::jthread{[&] { second->run(); }},
        std::jthread{[&] { third->run(); }},
        std::jthread{[&] { fourth->run(); }}};

    while (awaiting.load(std::memory_order_acquire) != waiter_count)
        std::this_thread::yield();

    // Some waiters are already queued while others may be between the failed
    // fast-path CAS and the queue-state exchange. This is the lost-wakeup
    // boundary exercised by the three-state mutex implementation.
    mtx.unlock();

    for (auto& runner : runners)
        runner.join();

    ASSERT_EQ(completed.load(std::memory_order_acquire), waiter_count);
    ASSERT_EQ(overlap_count.load(std::memory_order_acquire), 0U);
    ASSERT_EQ(wrong_loop_count.load(std::memory_order_acquire), 0U);
    ASSERT_EQ(inside.load(std::memory_order_acquire), 0U);
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

RUN_TESTS()
