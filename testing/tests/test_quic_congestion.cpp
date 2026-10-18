#include "test_framework.hpp"

import std;

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

TEST(congestion_controller_discarded_packets_only_release_flight_accounting)
{
    auto config = make_cc_config();
    cnetmod::quic::new_reno_congestion_controller cc(config);
    const auto initial_window = cc.congestion_window();

    cc.on_packet_sent(4096U);
    cc.on_packets_discarded(4096U);

    ASSERT_EQ(cc.bytes_in_flight(), 0U);
    ASSERT_EQ(cc.congestion_window(), initial_window);
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

TEST(cubic_congestion_event_does_not_reduce_twice_during_recovery)
{
    auto config = make_cc_config();
    cnetmod::quic::cubic_congestion_controller cc(config);

    cc.on_packet_sent(12000);
    cc.on_congestion_event(1000);
    const auto after_first_loss = cc.congestion_window();
    cc.on_congestion_event(1000);

    ASSERT_EQ(cc.congestion_window(), after_first_loss);
}

TEST(congestion_controller_selects_configured_cubic_without_heap_polymorphism)
{
    auto config = make_cc_config();
    config.congestion_algorithm = cnetmod::quic::quic_congestion_algorithm::cubic;
    auto cc = cnetmod::quic::create_congestion_controller(config);

    ASSERT_TRUE(cc.algorithm() == cnetmod::quic::quic_congestion_algorithm::cubic);
    cc.on_packet_sent(12000);
    cc.on_packet_acked(12000);
    const auto before_loss = cc.congestion_window();
    cc.on_congestion_event(2000);
    ASSERT_TRUE(cc.congestion_window() < before_loss);
}

TEST(congestion_controller_uses_cubic_as_the_default)
{
    auto cc = cnetmod::quic::create_congestion_controller(make_cc_config());

    ASSERT_TRUE(cc.algorithm() == cnetmod::quic::quic_congestion_algorithm::cubic);
}

TEST(bbr_selects_model_based_controller_and_tracks_acknowledgements)
{
    auto config = make_cc_config();
    config.congestion_algorithm = cnetmod::quic::quic_congestion_algorithm::bbr;
    auto cc = cnetmod::quic::create_congestion_controller(config);

    ASSERT_TRUE(cc.algorithm() == cnetmod::quic::quic_congestion_algorithm::bbr);
    const auto initial_window = cc.congestion_window();
    cc.on_packet_sent(12000U);
    cc.on_packet_acked(12000U);
    ASSERT_TRUE(cc.bytes_in_flight() == 0U);
    ASSERT_TRUE(cc.congestion_window() >= initial_window);
    ASSERT_TRUE(*cc.pacing_rate() > 0.0);
}

TEST(bbr_loss_keeps_a_safe_send_window)
{
    auto config = make_cc_config();
    config.congestion_algorithm = cnetmod::quic::quic_congestion_algorithm::bbr;
    auto cc = cnetmod::quic::create_congestion_controller(config);
    cc.on_packet_sent(12000U);
    cc.on_packet_acked(12000U);
    const auto before_loss = cc.congestion_window();
    cc.on_congestion_event(1000U);

    ASSERT_TRUE(cc.congestion_window() < before_loss);
    ASSERT_TRUE(cc.congestion_window() >= 2944U);
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


TEST(congestion_consecutive_losses_release_flight_during_recovery)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_cc_config();
        config.congestion_algorithm = algorithm;
        auto cc = cnetmod::quic::create_congestion_controller(config);
        cc.on_packet_sent(2000U);
        cc.on_congestion_event(1000U);
        ASSERT_EQ(cc.bytes_in_flight(), 1000U);
        const auto after_first_loss = cc.congestion_window();
        cc.on_congestion_event(1000U);
        ASSERT_EQ(cc.bytes_in_flight(), 0U);
        ASSERT_TRUE(cc.can_send_datagram(cc.congestion_window()));
        if (algorithm != cnetmod::quic::quic_congestion_algorithm::bbr)
            ASSERT_EQ(cc.congestion_window(), after_first_loss);
        else
            ASSERT_TRUE(cc.congestion_window() <= after_first_loss);
    }
}

TEST(congestion_ceiling_does_not_change_legacy_initial_window)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_cc_config();
        config.max_path_mtu = 65507U;
        config.max_udp_payload_size = 65527U;
        config.congestion_algorithm = algorithm;
        auto cc = cnetmod::quic::create_congestion_controller(config);
        ASSERT_EQ(cc.congestion_window(), 14720U);
    }
}

TEST(congestion_validated_mds_growth_preserves_mature_window_and_flight)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_cc_config();
        config.congestion_algorithm = algorithm;
        auto cc = cnetmod::quic::create_congestion_controller(config);
        cc.on_packet_sent(8192U);
        cc.on_packet_acked(8192U);
        const auto mature = cc.congestion_window();
        cc.on_packet_sent(100U);
        cc.on_max_datagram_size_validated(4096U);
        ASSERT_EQ(cc.congestion_window(), mature);
        ASSERT_EQ(cc.bytes_in_flight(), 100U);
        ASSERT_TRUE(cc.can_send_datagram(mature - 100U));
        ASSERT_FALSE(cc.can_send_datagram(mature - 99U));
        cc.on_max_datagram_size_validated(4096U);
        ASSERT_EQ(cc.congestion_window(), mature);
        ASSERT_EQ(cc.bytes_in_flight(), 100U);
    }
}

TEST(congestion_large_validated_mds_sets_two_packet_floor_without_erasing_flight)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_cc_config();
        config.congestion_algorithm = algorithm;
        auto cc = cnetmod::quic::create_congestion_controller(config);
        cc.on_packet_sent(100U);
        cc.on_max_datagram_size_validated(65507U);
        ASSERT_EQ(cc.congestion_window(), 131014U);
        ASSERT_EQ(cc.bytes_in_flight(), 100U);
        ASSERT_TRUE(cc.can_send_datagram(65507U));
        ASSERT_TRUE(cc.can_send_datagram(130914U));
        ASSERT_FALSE(cc.can_send_datagram(130915U));
        cc.on_congestion_event(100U);
        ASSERT_EQ(cc.congestion_window(), 131014U);
        ASSERT_TRUE(cc.can_send_datagram(131014U));
    }
}

TEST(congestion_loss_floor_tracks_ack_validated_mds_for_each_algorithm)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_cc_config();
        config.congestion_algorithm = algorithm;
        auto cc = cnetmod::quic::create_congestion_controller(config);
        cc.on_max_datagram_size_validated(4096U);
        for (std::size_t epoch = 0; epoch < 16U; ++epoch)
        {
            cc.on_packet_sent(100U);
            cc.on_congestion_event(100U);
            ASSERT_TRUE(cc.congestion_window() >= 8192U);
            cc.on_packet_sent(1U);
            cc.on_packet_acked(1U);
        }
        cc.on_packet_sent(100U);
        cc.on_congestion_event(100U);
        ASSERT_EQ(cc.congestion_window(), 8192U);
        ASSERT_EQ(cc.bytes_in_flight(), 0U);
        ASSERT_TRUE(cc.can_send_datagram(4096U));
    }
}

TEST(congestion_reno_ca_growth_uses_validated_mds)
{
    cnetmod::quic::new_reno_congestion_controller cc(make_cc_config());
    cc.on_max_datagram_size_validated(4096U);
    cc.on_congestion_event(0U);
    cc.on_packet_sent(100U);
    cc.on_packet_acked(100U); // Leave recovery; this ACK does not grow cwnd.
    const auto before = cc.congestion_window();
    cc.on_packet_sent(4096U);
    cc.on_packet_acked(4096U);
    ASSERT_EQ(cc.congestion_window(), before + (4096U * 4096U) / before);
}

TEST(quic_recovery_validated_mds_is_path_local)
{
    auto config = make_cc_config();
    config.congestion_algorithm = cnetmod::quic::quic_congestion_algorithm::new_reno;
    cnetmod::quic::quic_recovery_controller path_a(config);
    cnetmod::quic::quic_recovery_controller path_b(config);
    const auto now = std::chrono::steady_clock::now();
    path_a.on_packet_sent(1U, 8192U, now, true,
        cnetmod::quic::pn_space::application, {}, 8192U);
    path_b.on_packet_sent(1U, 8192U, now, true,
        cnetmod::quic::pn_space::application, {}, 8192U);
    ASSERT_EQ(path_a.congestion_window(), 14720U);
    ASSERT_EQ(path_b.congestion_window(), 14720U);
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 1U;
    const auto update = path_a.on_ack_received(
        ack, now + 20ms, cnetmod::quic::pn_space::application);
    ASSERT_TRUE(update.has_value());
    ASSERT_EQ(update->acknowledged_path_mtu_probes.size(), 1U);
    ASSERT_EQ(update->acknowledged_path_mtu_probes.front(), 8192U);
    const auto before_publish = path_a.congestion_window();
    path_a.on_max_datagram_size_validated(8192U);
    ASSERT_EQ(path_a.congestion_window(), before_publish);
    ASSERT_TRUE(path_a.can_send_datagram(before_publish));
    ASSERT_EQ(path_b.congestion_window(), 14720U);
    ASSERT_FALSE(path_b.can_send_datagram(path_b.congestion_window()));
    ASSERT_TRUE(path_b.next_pto_deadline().has_value());
}

TEST(congestion_first_validated_probe_can_be_below_legacy_initial_mss)
{
    cnetmod::quic::new_reno_congestion_controller cc(make_cc_config());
    cc.on_max_datagram_size_validated(1328U);
    ASSERT_EQ(cc.congestion_window(), 14720U);
    for (std::size_t epoch = 0; epoch < 4U; ++epoch)
    {
        cc.on_packet_sent(100U);
        cc.on_congestion_event(100U);
        cc.on_packet_sent(1U);
        cc.on_packet_acked(1U);
    }
    ASSERT_EQ(cc.congestion_window(), 2656U);
}


TEST(congestion_validated_mds_rejects_out_of_range_inputs_without_mutation)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_cc_config();
        config.congestion_algorithm = algorithm;
        auto cc = cnetmod::quic::create_congestion_controller(config);
        cc.on_packet_sent(100U);
        const auto before = cc.congestion_window();
        const auto pacing = cc.pacing_rate();
        for (const auto invalid : {std::uint64_t{0U}, std::uint64_t{1199U},
                 std::uint64_t{65528U}, (std::numeric_limits<std::uint64_t>::max)()})
        {
            cc.on_max_datagram_size_validated(invalid);
            ASSERT_EQ(cc.congestion_window(), before);
            ASSERT_EQ(cc.bytes_in_flight(), 100U);
            ASSERT_TRUE(cc.pacing_rate() == pacing);
        }
    }
}

RUN_TESTS();
