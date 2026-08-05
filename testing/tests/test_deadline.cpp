/// cnetmod unit tests -- deadline and cancellation cause

#include "test_framework.hpp"

import std;
import cnetmod.coro;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.core.dns;
import cnetmod.core.error;

using namespace cnetmod;

TEST(deadline_constrains_child_budget)
{
    const auto parent = deadline::after(std::chrono::seconds{2});
    const auto child = deadline::after(std::chrono::seconds{10});
    const auto constrained = parent.constrain(child);
    ASSERT_TRUE(constrained.at_time() <= parent.at_time());

    const deadline unlimited;
    ASSERT_TRUE(unlimited.is_unlimited());
    ASSERT_FALSE(unlimited.expired());
    ASSERT_TRUE(unlimited.constrain(parent).at_time() == parent.at_time());
}

TEST(cancel_token_retains_first_cancellation_cause)
{
    cancel_token caller;
    caller.cancel();
    caller.cancel_due_to_deadline();
    ASSERT_TRUE(caller.is_cancelled());
    ASSERT_EQ(static_cast<int>(caller.reason()),
        static_cast<int>(cancellation_reason::caller_cancelled));

    cancel_token timed_out;
    timed_out.cancel_due_to_deadline();
    timed_out.cancel();
    ASSERT_EQ(static_cast<int>(timed_out.reason()),
        static_cast<int>(cancellation_reason::deadline_exceeded));
}

TEST(with_deadline_expired_budget_normalizes_to_timeout)
{
    auto context = make_io_context();
    cancel_token token;
    bool started = false;
    auto operation = [&]() -> task<std::expected<int, std::error_code>> {
        started = true;
        co_return 42;
    };

    auto result = sync_wait(with_deadline(*context,
        deadline::at(std::chrono::steady_clock::now()), operation(), token));
    ASSERT_FALSE(started);
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::make_error_code(std::errc::timed_out));
    ASSERT_EQ(static_cast<int>(token.reason()),
        static_cast<int>(cancellation_reason::deadline_exceeded));
}

TEST(with_timeout_uses_deadline_cancellation_cause)
{
    auto context = make_io_context();
    cancel_token token;
    auto operation = []() -> task<std::expected<int, std::error_code>> {
        co_return 42;
    };

    auto result = sync_wait(with_timeout(*context, std::chrono::nanoseconds::zero(),
        operation(), token));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::make_error_code(std::errc::timed_out));
    ASSERT_EQ(static_cast<int>(token.reason()),
        static_cast<int>(cancellation_reason::deadline_exceeded));
}

TEST(with_deadline_factory_owns_a_fresh_cancel_token)
{
    auto context = make_io_context();
    cancel_token* observed = nullptr;
    auto result = sync_wait(with_deadline(*context, deadline::after(std::chrono::seconds{1}),
        [&](cancel_token& token) -> task<std::expected<int, std::error_code>> {
            observed = &token;
            co_return 42;
        }));
    ASSERT_TRUE(observed != nullptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
}

TEST(with_deadline_cancels_underlying_wait)
{
    auto context = make_io_context();
    cancel_token token;
    std::optional<std::expected<int, std::error_code>> result;

    auto operation = [&]() -> task<std::expected<int, std::error_code>> {
        auto waited = co_await async_timer_wait(*context, std::chrono::seconds{10}, token);
        if (!waited)
            co_return std::unexpected(waited.error());
        co_return 42;
    };
    auto wrapper = [&]() -> task<void> {
        result = co_await with_deadline(*context,
            deadline::after(std::chrono::milliseconds{1}), operation(), token);
        context->stop();
    };

    auto running = wrapper();
    running.handle().resume();
    context->run();

    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
    ASSERT_EQ(result->error(), std::make_error_code(std::errc::timed_out));
    ASSERT_EQ(static_cast<int>(token.reason()),
        static_cast<int>(cancellation_reason::deadline_exceeded));
}

TEST(happy_eyeballs_honors_pre_cancelled_token)
{
    auto context = make_io_context();
    cancel_token token;
    token.cancel_due_to_deadline();
    auto result = sync_wait(async_connect_happy_eyeballs(
        *context, "localhost", 443, {}, token));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), make_error_code(errc::operation_aborted));
}

RUN_TESTS()
