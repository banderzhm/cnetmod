/// cnetmod unit tests — task<T>, task<void>, sync_wait, when_all

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;

using namespace cnetmod;

// =============================================================================
// Helper coroutines
// =============================================================================

static auto return_42() -> task<int> {
    co_return 42;
}

static auto return_string() -> task<std::string> {
    co_return std::string("hello");
}

static auto return_void() -> task<void> {
    co_return;
}

static auto add(int a, int b) -> task<int> {
    co_return a + b;
}

static auto chain_add() -> task<int> {
    auto x = co_await return_42();
    auto y = co_await add(x, 8);
    co_return y;
}

static auto throwing_task() -> task<int> {
    throw std::runtime_error("test error");
    co_return 0;
}

static auto void_throwing_task() -> task<void> {
    throw std::runtime_error("void error");
    co_return;
}

// =============================================================================
// Tests
// =============================================================================

TEST(task_int_basic) {
    auto result = sync_wait(return_42());
    ASSERT_EQ(result, 42);
}

TEST(task_string_basic) {
    auto result = sync_wait(return_string());
    ASSERT_EQ(result, std::string("hello"));
}

TEST(task_void_basic) {
    // Should not throw
    sync_wait(return_void());
    ASSERT_TRUE(true);
}

TEST(task_chained_await) {
    auto result = sync_wait(chain_add());
    ASSERT_EQ(result, 50);
}

TEST(task_exception_propagation) {
    ASSERT_THROWS(sync_wait(throwing_task()));
}

TEST(task_void_exception_propagation) {
    ASSERT_THROWS(sync_wait(void_throwing_task()));
}

TEST(task_move_semantics) {
    auto t = return_42();
    auto t2 = std::move(t);
    auto result = sync_wait(std::move(t2));
    ASSERT_EQ(result, 42);
}

TEST(when_all_two_ints) {
    auto both = when_all(return_42(), add(10, 20));
    auto [a, b] = sync_wait(std::move(both));
    ASSERT_EQ(a, 42);
    ASSERT_EQ(b, 30);
}

TEST(when_all_three_ints) {
    auto all = when_all(return_42(), add(1, 2), add(10, 10));
    auto [a, b, c] = sync_wait(std::move(all));
    ASSERT_EQ(a, 42);
    ASSERT_EQ(b, 3);
    ASSERT_EQ(c, 20);
}

TEST(when_all_void) {
    // Should not throw
    sync_wait(when_all(return_void(), return_void()));
    ASSERT_TRUE(true);
}

// =============================================================================
// Extended Tests
// =============================================================================

static auto return_expected_ok() -> task<std::expected<int, std::string>> {
    co_return 42;
}

static auto return_expected_err() -> task<std::expected<int, std::string>> {
    co_return std::unexpected(std::string("fail"));
}

TEST(task_expected_ok) {
    auto result = sync_wait(return_expected_ok());
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
}

TEST(task_expected_error) {
    auto result = sync_wait(return_expected_err());
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::string("fail"));
}

// 10-level nested co_await
template <int N>
static auto nested() -> task<int> {
    if constexpr (N <= 0)
        co_return 1;
    else
        co_return co_await nested<N - 1>() + 1;
}

TEST(task_nested_co_await_10) {
    auto result = sync_wait(nested<10>());
    ASSERT_EQ(result, 11);
}

TEST(task_move_only_type) {
    auto make_ptr = []() -> task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(99);
    };
    auto result = sync_wait(make_ptr());
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(*result, 99);
}

TEST(when_all_mixed_types) {
    auto [i, s] = sync_wait(when_all(return_42(), return_string()));
    ASSERT_EQ(i, 42);
    ASSERT_EQ(s, std::string("hello"));
}

TEST(when_all_exception_propagation) {
    // Exception in one task should propagate when extracting result
    bool caught = false;
    try {
        auto [a, b] = sync_wait(when_all(throwing_task(), return_42()));
        (void)a; (void)b;
    } catch (const std::runtime_error&) {
        caught = true;
    }
    ASSERT_TRUE(caught);
}

TEST(when_all_single_task) {
    // when_all requires >= 2 tasks; test single task via direct sync_wait
    auto r = sync_wait(return_42());
    ASSERT_EQ(r, 42);
}

RUN_TESTS()
