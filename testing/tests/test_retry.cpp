/// cnetmod unit tests — retry count, backoff behavior, early success return

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.coro.retry;
import cnetmod.io.io_context;

using namespace cnetmod;

// =============================================================================
// Tests using sync_wait (first attempt success — no io_context event loop needed)
// =============================================================================

TEST(retry_options_defaults) {
    retry_options opts;
    ASSERT_EQ(opts.max_attempts, (std::uint32_t)3);
    ASSERT_TRUE(opts.multiplier == 2.0);
    ASSERT_TRUE(opts.jitter);
}

TEST(retry_first_try_success) {
    auto ctx = make_io_context();

    auto op = []() -> task<std::expected<int, std::error_code>> {
        co_return 42;
    };

    auto result = sync_wait(retry<int, std::error_code>(*ctx, {}, op));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
}

TEST(retry_throwing_first_try_success) {
    auto ctx = make_io_context();

    auto op = []() -> task<int> {
        co_return 99;
    };

    auto result = sync_wait(retry_throwing(*ctx, {}, op));
    ASSERT_EQ(result, 99);
}

TEST(retry_single_attempt_failure) {
    auto ctx = make_io_context();
    int attempts = 0;

    auto op = [&]() -> task<std::expected<int, std::error_code>> {
        ++attempts;
        co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
    };

    // max_attempts = 1: try once, no retry, no sleep
    auto result = sync_wait(retry<int, std::error_code>(*ctx, {.max_attempts = 1}, op));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(attempts, 1);
}

// =============================================================================
// Tests with io_context event loop (multi-retry — needs real timer driving)
//
// Pattern: start wrapper coroutine with handle.resume(), then ctx->run()
// drives timers. Wrapper stores result and calls ctx->stop() on completion.
// =============================================================================

TEST(retry_succeeds_after_failures) {
    auto ctx = make_io_context();
    int attempts = 0;

    auto op = [&]() -> task<std::expected<int, std::error_code>> {
        ++attempts;
        if (attempts < 3)
            co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
        co_return 42;
    };

    std::optional<std::expected<int, std::error_code>> result;

    auto wrapper = [&]() -> task<void> {
        result = co_await retry<int, std::error_code>(*ctx, {
            .max_attempts = 5,
            .initial_delay = std::chrono::milliseconds(1),
            .max_delay = std::chrono::milliseconds(10),
            .jitter = false,
        }, op);
        ctx->stop();
    };

    auto w = wrapper();
    w.handle().resume();
    ctx->run();

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    ASSERT_EQ(**result, 42);
    ASSERT_EQ(attempts, 3);
}

TEST(retry_exhausts_all_attempts) {
    auto ctx = make_io_context();
    int attempts = 0;

    auto op = [&]() -> task<std::expected<int, std::error_code>> {
        ++attempts;
        co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
    };

    std::optional<std::expected<int, std::error_code>> result;

    auto wrapper = [&]() -> task<void> {
        result = co_await retry<int, std::error_code>(*ctx, {
            .max_attempts = 3,
            .initial_delay = std::chrono::milliseconds(1),
            .max_delay = std::chrono::milliseconds(10),
            .jitter = false,
        }, op);
        ctx->stop();
    };

    auto w = wrapper();
    w.handle().resume();
    ctx->run();

    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
    ASSERT_EQ(attempts, 3);
}

TEST(retry_throwing_succeeds_after_failures) {
    auto ctx = make_io_context();
    int attempts = 0;

    auto op = [&]() -> task<int> {
        ++attempts;
        if (attempts < 3)
            throw std::runtime_error("transient failure");
        co_return 42;
    };

    std::optional<int> result;

    auto wrapper = [&]() -> task<void> {
        result = co_await retry_throwing(*ctx, {
            .max_attempts = 5,
            .initial_delay = std::chrono::milliseconds(1),
            .max_delay = std::chrono::milliseconds(10),
            .jitter = false,
        }, op);
        ctx->stop();
    };

    auto w = wrapper();
    w.handle().resume();
    ctx->run();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
    ASSERT_EQ(attempts, 3);
}

TEST(retry_throwing_exhausts_then_rethrows) {
    auto ctx = make_io_context();
    int attempts = 0;

    auto op = [&]() -> task<int> {
        ++attempts;
        throw std::runtime_error("always fails");
        co_return 0;  // unreachable, but needed for coroutine
    };

    bool caught = false;

    auto wrapper = [&]() -> task<void> {
        try {
            (void)co_await retry_throwing(*ctx, {
                .max_attempts = 3,
                .initial_delay = std::chrono::milliseconds(1),
                .max_delay = std::chrono::milliseconds(10),
                .jitter = false,
            }, op);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        ctx->stop();
    };

    auto w = wrapper();
    w.handle().resume();
    ctx->run();

    ASSERT_TRUE(caught);
    ASSERT_EQ(attempts, 3);
}

RUN_TESTS()
