/// cnetmod unit tests -- structured task group cancellation and join

#include "test_framework.hpp"

import std;
import cnetmod.coro;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

using namespace cnetmod;

TEST(task_group_first_error_cancels_and_joins_children)
{
    auto context = make_io_context();
    task_group group{*context};
    bool sibling_cancelled = false;

    ASSERT_TRUE(group.run([](cancel_token&) -> task<std::expected<void, std::error_code>> {
        co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
    }));
    ASSERT_TRUE(group.run([&](cancel_token& token)
        -> task<std::expected<void, std::error_code>> {
        auto result = co_await async_timer_wait(*context, std::chrono::seconds{10}, token);
        sibling_cancelled = !result && token.is_cancelled();
        if (!result)
            co_return std::unexpected(result.error());
        co_return std::expected<void, std::error_code>{};
    }));

    std::optional<std::expected<void, std::error_code>> joined;
    auto waiter = [&]() -> task<void> {
        joined = co_await group.join();
        context->stop();
    };
    auto running = waiter();
    running.handle().resume();
    context->run();

    ASSERT_TRUE(joined.has_value());
    ASSERT_FALSE(joined->has_value());
    ASSERT_EQ(joined->error(), std::make_error_code(std::errc::connection_refused));
    ASSERT_TRUE(sibling_cancelled);
}

TEST(task_group_deadline_cancels_and_joins_children)
{
    auto context = make_io_context();
    task_group group{*context, deadline::after(std::chrono::milliseconds{1})};
    bool deadline_cancelled = false;

    ASSERT_TRUE(group.run([&](cancel_token& token)
        -> task<std::expected<void, std::error_code>> {
        auto result = co_await async_timer_wait(*context, std::chrono::seconds{10}, token);
        deadline_cancelled = !result &&
            token.reason() == cancellation_reason::deadline_exceeded;
        if (!result)
            co_return std::unexpected(result.error());
        co_return std::expected<void, std::error_code>{};
    }));

    std::optional<std::expected<void, std::error_code>> joined;
    auto waiter = [&]() -> task<void> {
        joined = co_await group.join();
        context->stop();
    };
    auto running = waiter();
    running.handle().resume();
    context->run();

    ASSERT_TRUE(joined.has_value());
    ASSERT_FALSE(joined->has_value());
    ASSERT_EQ(joined->error(), std::make_error_code(std::errc::timed_out));
    ASSERT_TRUE(deadline_cancelled);
}

RUN_TESTS()
