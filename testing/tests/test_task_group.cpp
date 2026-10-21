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

TEST(task_group_reuses_retired_children_and_cancels_only_live_operations)
{
    auto context = make_io_context();
    task_group group{*context};
    std::size_t completed{}, cancelled{};
    std::atomic<bool> cancellation_ready{false};
    auto exercise = [&]() -> task<void>
    {
        for (unsigned wave{}; wave < 16; ++wave)
        {
            for (unsigned child{}; child < 32; ++child)
                ASSERT_TRUE(group.run([&](cancel_token& token)
                    -> task<std::expected<void, std::error_code>>
                    {
                        ASSERT_FALSE(token.is_cancelled());
                        ++completed;
                        co_return std::expected<void, std::error_code>{};
                    }));
            while (!group.completion_result())
                co_await post_awaitable{*context};
        }
        for (unsigned child{}; child < 8; ++child)
            ASSERT_TRUE(group.run([&](cancel_token& token)
                -> task<std::expected<void, std::error_code>>
                {
                    auto result = co_await async_timer_wait(
                        *context, std::chrono::seconds{10}, token);
                    if (!result && token.is_cancelled())
                        ++cancelled;
                    co_return result;
                }));
        // Let all current children register their platform operations before a
        // foreign control thread cancels the group, including reused slots.
        co_await post_awaitable{*context};
        cancellation_ready.store(true, std::memory_order_release);
        const auto joined = co_await group.join();
        ASSERT_FALSE(joined.has_value());
        context->stop();
    };
    std::thread control([&]
        {
            while (!cancellation_ready.load(std::memory_order_acquire))
                std::this_thread::yield();
            group.cancel();
        });
    auto running = exercise();
    running.handle().resume();
    context->run();
    control.join();
    ASSERT_TRUE(running.handle().done());
    running.handle().promise().result();
    ASSERT_EQ(completed, std::size_t{512});
    ASSERT_EQ(cancelled, std::size_t{8});
}

RUN_TESTS()
