#include "test_framework.hpp"

#include <chrono>

import cnetmod.protocol.quic;

using namespace std::chrono_literals;

// =============================================================================
// Helper: create a default quic_config
// =============================================================================
auto make_cc_config() -> cnetmod::quic::quic_config
{
    cnetmod::quic::quic_config config;
    config.idle_timeout = std::chrono::milliseconds(30000);
    config.max_data = 1048576;
    config.max_stream_data = 262144;
    config.max_streams_bidi = 100;
    config.max_streams_uni = 100;
    config.cid_length = 8;
    return config;
}

// =============================================================================
// Tests: Initial state
// =============================================================================

TEST(congestion_controller_initial_cwnd)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // Initial cwnd should be 14720 (10 MTUs per RFC 9002 recommendation)
    ASSERT_EQ(cc.congestion_window(), 14720ULL);
}

TEST(congestion_controller_initial_ssthresh)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // Initial ssthresh should be effectively infinity (UINT64_MAX)
    ASSERT_EQ(cc.ssthresh(), std::uint64_t(~0ULL));
}

TEST(congestion_controller_initial_pacing_rate)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // Pacing rate should be available (conservative default)
    auto rate = cc.pacing_rate();
    ASSERT_TRUE(rate.has_value());
    ASSERT_TRUE(*rate > 0.0);
}

// =============================================================================
// Tests: can_send logic
// =============================================================================

TEST(congestion_controller_can_send_below_cwnd)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // cwnd = 14720, bytes_in_flight = 0 → can send
    ASSERT_TRUE(cc.can_send(0));

    // bytes_in_flight = 10000 < cwnd → can send
    ASSERT_TRUE(cc.can_send(10000));

    // bytes_in_flight = 14719 < cwnd → can send
    ASSERT_TRUE(cc.can_send(14719));
}

TEST(congestion_controller_can_send_at_cwnd)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // bytes_in_flight = cwnd → cannot send
    ASSERT_FALSE(cc.can_send(14720));

    // bytes_in_flight > cwnd → cannot send
    ASSERT_FALSE(cc.can_send(20000));
}

// =============================================================================
// Tests: Packet sent/acked tracking
// =============================================================================

TEST(congestion_controller_on_packet_sent)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // Send some packets
    cc.on_packet_sent(1000);
    cc.on_packet_sent(2000);
    cc.on_packet_sent(1500);

    // Should still be able to send (total in-flight = 4500 < 14720)
    ASSERT_TRUE(cc.can_send(4500));

    // But not if we've used up the window
    ASSERT_FALSE(cc.can_send(14720));
}

TEST(congestion_controller_on_packet_acked)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // Send packets totalingalling 10000 bytes
    cc.on_packet_sent(10000);

    // Can't send more at cwnd limit
    ASSERT_FALSE(cc.can_send(14720));

    // ACK some bytes
    cc.on_packet_acked(5000);

    // Now bytes_in_flight = 5000, can send more
    ASSERT_TRUE(cc.can_send(5000));
}

// =============================================================================
// Tests: Congestion event handling
// =============================================================================

TEST(congestion_controller_congestion_event_no_crash)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // Trigger congestion event
    cc.on_congestion_event(1000);

    // Should not crash, cwnd should still be accessible
    auto cwnd = cc.congestion_window();
    ASSERT_TRUE(cwnd > 0);
}

TEST(congestion_controller_multiple_congestion_events)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // Multiple congestion events should not crash
    for (int i = 0; i < 5; ++i)
    {
        cc.on_packet_sent(1000);
        cc.on_congestion_event(500);
        cc.on_packet_acked(500);
    }

    // cwnd should still be positive
    ASSERT_TRUE(cc.congestion_window() > 0);
}

TEST(congestion_controller_recovery_re_entry_prevention)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // First congestion event enters recovery
    cc.on_congestion_event(1000);

    auto cwnd_after_first = cc.congestion_window();

    // Second congestion event while in recovery should be a no-op
    cc.on_congestion_event(1000);

    auto cwnd_after_second = cc.congestion_window();

    // cwnd should be the same (re-entry prevention)
    ASSERT_EQ(cwnd_after_first, cwnd_after_second);
}

// =============================================================================
// Tests: cwnd bounds
// =============================================================================

TEST(congestion_controller_cwnd_positive)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    // After various operations, cwnd should always be positive
    ASSERT_TRUE(cc.congestion_window() > 0);

    cc.on_packet_sent(14000);
    ASSERT_TRUE(cc.congestion_window() > 0);

    cc.on_congestion_event(7000);
    ASSERT_TRUE(cc.congestion_window() > 0);

    cc.on_packet_acked(7000);
    ASSERT_TRUE(cc.congestion_window() > 0);
}

TEST(cubic_reduces_window_and_preserves_minimum)
{
    auto config = make_cc_config();
    cnetmod::quic::cubic_congestion_controller cc(config);

    cc.on_packet_sent(12000);
    cc.on_packet_acked(12000);
    const auto before_loss = cc.congestion_window();
    cc.on_congestion_event(2000);

    ASSERT_TRUE(cc.congestion_window() < before_loss);
    ASSERT_TRUE(cc.congestion_window() >= 2944ULL);
}

TEST(congestion_pacing_tracks_rtt)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);

    cc.update_rtt(20ms);
    const auto fast_rate = *cc.pacing_rate();
    cc.update_rtt(200ms);
    const auto slow_rate = *cc.pacing_rate();

    ASSERT_TRUE(fast_rate > slow_rate);
}

RUN_TESTS();
