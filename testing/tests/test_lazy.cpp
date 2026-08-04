/// cnetmod unit tests -- lazy<T> and lazy<void>

#include "test_framework.hpp"

import std;
import cnetmod.core.lazy;
import cnetmod.coro.task;

using namespace cnetmod;

static auto lazy_value(int& executions) -> lazy<int>
{
    ++executions;
    co_return 42;
}

static auto lazy_void(bool& completed) -> lazy<void>
{
    completed = true;
    co_return;
}

static auto lazy_error() -> lazy<int>
{
    throw std::runtime_error("lazy error");
    co_return 0;
}

static auto await_lazy_value(int& executions) -> task<int>
{
    co_return co_await lazy_value(executions);
}

static auto await_lazy(lazy<int> operation) -> task<int>
{
    co_return co_await std::move(operation);
}

static auto await_lazy_void(bool& completed) -> task<void>
{
    co_await lazy_void(completed);
}

static auto await_lazy_error() -> task<void>
{
    (void)co_await lazy_error();
}

TEST(lazy_is_cold_until_awaited)
{
    int executions = 0;
    auto operation = lazy_value(executions);
    ASSERT_EQ(executions, 0);
    auto result = sync_wait(await_lazy(std::move(operation)));
    ASSERT_EQ(result, 42);
    ASSERT_EQ(executions, 1);
}

TEST(lazy_value_is_transferred_symmetrically)
{
    int executions = 0;
    ASSERT_EQ(sync_wait(await_lazy_value(executions)), 42);
    ASSERT_EQ(executions, 1);
}

TEST(lazy_void_propagates_completion)
{
    bool completed = false;
    sync_wait(await_lazy_void(completed));
    ASSERT_TRUE(completed);
}

TEST(lazy_exception_propagates_to_awaiter)
{
    ASSERT_THROWS(sync_wait(await_lazy_error()));
}

TEST(lazy_start_and_result)
{
    int executions = 0;
    auto operation = lazy_value(executions);
    operation.start();
    ASSERT_TRUE(operation.done());
    ASSERT_EQ(executions, 1);
    ASSERT_EQ(operation.result(), 42);
}

TEST(lazy_is_move_only)
{
    static_assert(!std::copy_constructible<lazy<int>>);
    static_assert(std::move_constructible<lazy<int>>);
    int executions = 0;
    auto first = lazy_value(executions);
    auto second = std::move(first);
    second.start();
    ASSERT_EQ(second.result(), 42);
}

RUN_TESTS()
