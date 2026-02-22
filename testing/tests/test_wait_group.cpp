/// cnetmod unit tests — async_wait_group add/done/wait, count tracking

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.coro.wait_group;

using namespace cnetmod;

// =============================================================================
// Tests
// =============================================================================

TEST(wg_initial_count_zero) {
    async_wait_group wg;
    ASSERT_EQ(wg.count(), 0);
}

TEST(wg_add_increases_count) {
    async_wait_group wg;
    wg.add(3);
    ASSERT_EQ(wg.count(), 3);
}

TEST(wg_done_decreases_count) {
    async_wait_group wg;
    wg.add(3);
    wg.done();
    ASSERT_EQ(wg.count(), 2);
    wg.done();
    ASSERT_EQ(wg.count(), 1);
    wg.done();
    ASSERT_EQ(wg.count(), 0);
}

TEST(wg_wait_already_zero) {
    // wait should return immediately when count is already 0
    async_wait_group wg;

    auto task_fn = [&]() -> task<void> {
        co_await wg.wait();
    };
    sync_wait(task_fn());
    ASSERT_TRUE(true);  // Did not hang
}

TEST(wg_add_done_then_wait) {
    // Complete all tasks before waiting — wait returns immediately
    async_wait_group wg;
    wg.add(3);
    wg.done();
    wg.done();
    wg.done();

    auto task_fn = [&]() -> task<void> {
        co_await wg.wait();
    };
    sync_wait(task_fn());
    ASSERT_EQ(wg.count(), 0);
}

TEST(wg_concurrent_done_and_wait) {
    // Use when_all: one coroutine waits, another does add+done
    async_wait_group wg;
    wg.add(2);

    auto worker1 = [&]() -> task<int> {
        wg.done();
        co_return 1;
    };

    auto worker2 = [&]() -> task<int> {
        wg.done();
        co_return 2;
    };

    // Workers complete, then wait should succeed
    auto [a, b] = sync_wait(when_all(worker1(), worker2()));
    ASSERT_EQ(a, 1);
    ASSERT_EQ(b, 2);
    ASSERT_EQ(wg.count(), 0);

    // Now wait should return immediately
    auto wait_task = [&]() -> task<void> {
        co_await wg.wait();
    };
    sync_wait(wait_task());
}

TEST(wg_add_incremental) {
    async_wait_group wg;
    wg.add(1);
    wg.add(1);
    wg.add(1);
    ASSERT_EQ(wg.count(), 3);

    wg.done();
    wg.done();
    wg.done();
    ASSERT_EQ(wg.count(), 0);
}

TEST(wg_multiple_add_values) {
    async_wait_group wg;
    wg.add(5);
    ASSERT_EQ(wg.count(), 5);

    for (int i = 0; i < 5; ++i)
        wg.done();

    ASSERT_EQ(wg.count(), 0);
}

RUN_TESTS()
