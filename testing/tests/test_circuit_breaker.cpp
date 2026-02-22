/// cnetmod unit tests — circuit_breaker state transitions, thresholds, timeout recovery

#include "test_framework.hpp"

import cnetmod.coro.task;
import cnetmod.coro.circuit_breaker;

using namespace cnetmod;

// =============================================================================
// Helper coroutines
// =============================================================================

static auto success_op() -> task<std::expected<int, std::error_code>> {
    co_return 42;
}

static auto failure_op() -> task<std::expected<int, std::error_code>> {
    co_return std::unexpected(make_error_code(std::errc::connection_refused));
}

// =============================================================================
// Tests
// =============================================================================

TEST(cb_initial_state_closed) {
    circuit_breaker cb;
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
    ASSERT_EQ(cb.failure_count(), (std::uint32_t)0);
}

TEST(cb_success_stays_closed) {
    circuit_breaker cb({.failure_threshold = 3});

    auto result = sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
    ASSERT_EQ(cb.failure_count(), (std::uint32_t)0);
}

TEST(cb_failures_below_threshold) {
    circuit_breaker cb({.failure_threshold = 3});

    // 2 failures — still closed
    sync_wait(cb.execute_ec<int>(failure_op));
    sync_wait(cb.execute_ec<int>(failure_op));

    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
    ASSERT_EQ(cb.failure_count(), (std::uint32_t)2);
}

TEST(cb_failures_reach_threshold_opens) {
    circuit_breaker cb({.failure_threshold = 3});

    sync_wait(cb.execute_ec<int>(failure_op));
    sync_wait(cb.execute_ec<int>(failure_op));
    sync_wait(cb.execute_ec<int>(failure_op));

    ASSERT_TRUE(cb.state() == circuit_breaker_state::open);
}

TEST(cb_open_rejects_immediately) {
    circuit_breaker cb({.failure_threshold = 2, .timeout = std::chrono::hours(1)});

    // Trip the breaker
    sync_wait(cb.execute_ec<int>(failure_op));
    sync_wait(cb.execute_ec<int>(failure_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::open);

    // Next call should be rejected without calling the function
    bool called = false;
    auto guarded_op = [&]() -> task<std::expected<int, std::error_code>> {
        called = true;
        co_return 99;
    };
    auto result = sync_wait(cb.execute_ec<int>(guarded_op));
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(called);  // Operation should not have been called
}

TEST(cb_success_resets_failure_count) {
    circuit_breaker cb({.failure_threshold = 3});

    sync_wait(cb.execute_ec<int>(failure_op));
    sync_wait(cb.execute_ec<int>(failure_op));
    ASSERT_EQ(cb.failure_count(), (std::uint32_t)2);

    // Success resets count
    sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_EQ(cb.failure_count(), (std::uint32_t)0);
}

TEST(cb_manual_trip) {
    circuit_breaker cb;
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);

    cb.trip();
    ASSERT_TRUE(cb.state() == circuit_breaker_state::open);
}

TEST(cb_manual_reset) {
    circuit_breaker cb({.failure_threshold = 1});

    sync_wait(cb.execute_ec<int>(failure_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::open);

    cb.reset();
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
    ASSERT_EQ(cb.failure_count(), (std::uint32_t)0);
}

TEST(cb_timeout_transitions_to_half_open) {
    // Use very short timeout so we can test the transition
    circuit_breaker cb({
        .failure_threshold = 1,
        .success_threshold = 1,
        .timeout = std::chrono::milliseconds(1),
    });

    // Trip
    sync_wait(cb.execute_ec<int>(failure_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::open);

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Should now be half_open
    ASSERT_TRUE(cb.state() == circuit_breaker_state::half_open);
}

TEST(cb_half_open_success_closes) {
    circuit_breaker cb({
        .failure_threshold = 1,
        .success_threshold = 1,
        .timeout = std::chrono::milliseconds(1),
    });

    // Trip → open
    sync_wait(cb.execute_ec<int>(failure_op));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::half_open);

    // Success in half_open → closed
    sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
}

TEST(cb_half_open_failure_reopens) {
    circuit_breaker cb({
        .failure_threshold = 1,
        .success_threshold = 2,
        .timeout = std::chrono::milliseconds(1),
    });

    // Trip → open → half_open
    sync_wait(cb.execute_ec<int>(failure_op));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::half_open);

    // Failure in half_open → back to open
    sync_wait(cb.execute_ec<int>(failure_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::open);
}

TEST(cb_half_open_needs_multiple_successes) {
    circuit_breaker cb({
        .failure_threshold = 1,
        .success_threshold = 3,
        .timeout = std::chrono::milliseconds(1),
    });

    // Trip → open → half_open
    sync_wait(cb.execute_ec<int>(failure_op));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // First success — still half_open
    sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::half_open);
    ASSERT_EQ(cb.success_count(), (std::uint32_t)1);

    // Second success — still half_open
    sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::half_open);
    ASSERT_EQ(cb.success_count(), (std::uint32_t)2);

    // Third success — closed!
    sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
}

// =============================================================================
// Extended Tests
// =============================================================================

TEST(cb_full_lifecycle) {
    // Complete cycle: closed → open → half_open → closed
    circuit_breaker cb({
        .failure_threshold = 2,
        .success_threshold = 1,
        .timeout = std::chrono::milliseconds(1),
    });

    // Phase 1: closed
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
    sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);

    // Phase 2: trip to open
    sync_wait(cb.execute_ec<int>(failure_op));
    sync_wait(cb.execute_ec<int>(failure_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::open);

    // Phase 3: timeout → half_open
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::half_open);

    // Phase 4: success in half_open → closed
    sync_wait(cb.execute_ec<int>(success_op));
    ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
    ASSERT_EQ(cb.failure_count(), (std::uint32_t)0);
}

TEST(cb_repeated_trip_and_recovery) {
    circuit_breaker cb({
        .failure_threshold = 1,
        .success_threshold = 1,
        .timeout = std::chrono::milliseconds(1),
    });

    for (int cycle = 0; cycle < 3; ++cycle) {
        // Trip
        sync_wait(cb.execute_ec<int>(failure_op));
        ASSERT_TRUE(cb.state() == circuit_breaker_state::open);

        // Wait for half_open
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_TRUE(cb.state() == circuit_breaker_state::half_open);

        // Recover
        sync_wait(cb.execute_ec<int>(success_op));
        ASSERT_TRUE(cb.state() == circuit_breaker_state::closed);
    }
}

RUN_TESTS()
