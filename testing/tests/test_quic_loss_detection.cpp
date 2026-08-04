#include "test_framework.hpp"

#include <chrono>

import cnetmod.protocol.quic;

using namespace std::chrono_literals;

// =============================================================================
// Helper: create a default quic_config
// =============================================================================
auto make_test_config() -> cnetmod::quic::quic_config
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
// Tests: RTT initialization
// =============================================================================

TEST(loss_detector_rtt_initialization)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    // Before initialization, smoothed_rtt should be zero
    auto& rtt = detector.rtt_estimate();
    ASSERT_TRUE(rtt.smoothed_rtt_ == std::chrono::steady_clock::duration{0});

    // Initialize with 100ms RTT
    detector.initialize_rtt(100ms);

    auto& rtt_after = detector.rtt_estimate();
    // smoothed_rtt = rtt - granularity(1ms) = 99ms
    auto smoothed_us = std::chrono::duration_cast<std::chrono::microseconds>(rtt_after.smoothed_rtt_).count();
    ASSERT_GT(smoothed_us, 0);

    // rtt_var = rtt / 2 = 50ms
    auto var_us = std::chrono::duration_cast<std::chrono::microseconds>(rtt_after.rtt_var_).count();
    ASSERT_GT(var_us, 0);

    // min_rtt should be updated to 100ms
    auto min_us = std::chrono::duration_cast<std::chrono::microseconds>(rtt_after.min_rtt_).count();
    ASSERT_EQ(min_us, 100000);
}

TEST(loss_detector_rtt_multiple_samples)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    // First sample: 100ms
    detector.initialize_rtt(100ms);

    auto& rtt1 = detector.rtt_estimate();
    auto smoothed1 = std::chrono::duration_cast<std::chrono::microseconds>(rtt1.smoothed_rtt_).count();

    // Second sample: 200ms (should increase smoothed_rtt)
    detector.initialize_rtt(200ms);

    auto& rtt2 = detector.rtt_estimate();
    auto smoothed2 = std::chrono::duration_cast<std::chrono::microseconds>(rtt2.smoothed_rtt_).count();

    // Smoothed RTT should have increased
    ASSERT_GT(smoothed2, smoothed1);

    // Min RTT should still be 100ms (from first sample)
    auto min_us = std::chrono::duration_cast<std::chrono::microseconds>(rtt2.min_rtt_).count();
    ASSERT_EQ(min_us, 100000);
}

// =============================================================================
// Tests: PTO duration
// =============================================================================

TEST(loss_detector_pto_duration_basic)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    // Initialize RTT
    detector.initialize_rtt(100ms);

    auto pto = detector.pto_duration();
    auto smoothed = detector.rtt_estimate().smoothed_rtt_;

    // PTO should be greater than smoothed_rtt
    ASSERT_TRUE(pto > smoothed);

    // PTO should be less than 1 second for reasonable RTT
    ASSERT_TRUE(pto < 1s);

    // PTO should be > 0
    ASSERT_TRUE(pto > std::chrono::steady_clock::duration{0});
}

TEST(loss_detector_pto_increases_with_rtt_var)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    // Initialize with stable RTT
    detector.initialize_rtt(100ms);
    auto pto1 = detector.pto_duration();

    // Initialize with higher variance RTT (update with different value)
    detector.initialize_rtt(200ms);
    auto pto2 = detector.pto_duration();

    // PTO after higher variance should be larger
    // (because rtt_var increases when RTT samples differ from smoothed)
    ASSERT_TRUE(pto2 > pto1);
}

// =============================================================================
// Tests: Packet tracking
// =============================================================================

TEST(loss_detector_packet_sent_tracking)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    auto now = std::chrono::steady_clock::now();

    // Send some packets
    detector.on_packet_sent(1, 100, now, true, cnetmod::quic::pn_space::application);
    detector.on_packet_sent(2, 200, now + 1ms, true, cnetmod::quic::pn_space::application);
    detector.on_packet_sent(3, 150, now + 2ms, true, cnetmod::quic::pn_space::application);

    // No loss detected yet (no ACKs received)
    auto lost = detector.detect_lost_packets(now + 10ms, cnetmod::quic::pn_space::application);
    // RFC 9002 loss detection is anchored by acknowledgement state; elapsed
    // time alone before the first ACK must not declare these packets lost.
    ASSERT_TRUE(lost.empty());
}

TEST(loss_detector_ack_received_processing)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    auto now = std::chrono::steady_clock::now();

    // Send a packet
    detector.on_packet_sent(1, 100, now, true, cnetmod::quic::pn_space::application);

    // Receive ACK for packet 1
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 1;
    ack.ack_delay = 0;
    ack.ack_range_count = 0;
    ack.first_ack_range = 0;

    auto result = detector.on_ack_received(
        ack, 1, now + 100ms, cnetmod::quic::pn_space::application);
    ASSERT_TRUE(result.has_value());

    // Packet 1 should be in the newly acked list
    ASSERT_EQ(result->size(), 1u);
    ASSERT_EQ((*result)[0], 1ULL);
}

TEST(loss_detector_ack_range_may_include_ack_only_packet)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);
    const auto now = std::chrono::steady_clock::now();

    detector.on_packet_sent(63, 100, now, true,
        cnetmod::quic::pn_space::application);
    detector.on_packet_sent(64, 40, now + 1ms, false,
        cnetmod::quic::pn_space::application);

    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 64;
    ack.first_ack_range = 1;

    const auto result = detector.on_ack_received(
        ack, 64, now + 10ms, cnetmod::quic::pn_space::application);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    ASSERT_EQ((*result)[0], 63ULL);
}

TEST(loss_detector_rejects_ack_for_unsent_packet)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);
    const auto now = std::chrono::steady_clock::now();

    detector.on_packet_sent(64, 40, now, false,
        cnetmod::quic::pn_space::application);

    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 65;

    const auto result = detector.on_ack_received(
        ack, 65, now + 10ms, cnetmod::quic::pn_space::application);
    ASSERT_TRUE(!result.has_value());
    ASSERT_EQ(result.error(),
        cnetmod::quic::make_error_code(cnetmod::quic::quic_errc::protocol_violation));
}

// =============================================================================
// Tests: Loss detection
// =============================================================================

TEST(loss_detector_no_loss_on_immediate_ack)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    detector.initialize_rtt(100ms);

    auto now = std::chrono::steady_clock::now();

    // Send packet and receive immediate ACK
    detector.on_packet_sent(1, 100, now, true, cnetmod::quic::pn_space::application);

    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 1;
    ack.ack_delay = 0;
    ack.ack_range_count = 0;
    ack.first_ack_range = 0;

    const auto acknowledged = detector.on_ack_received(
        ack, 1, now + 100ms, cnetmod::quic::pn_space::application);
    ASSERT_TRUE(acknowledged.has_value());

    // No packets should be lost
    auto lost = detector.detect_lost_packets(now + 100ms, cnetmod::quic::pn_space::application);
    ASSERT_EQ(lost.size(), 0u);
}

// =============================================================================
// Tests: Per-space isolation
// =============================================================================

TEST(loss_detector_pn_space_isolation)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    auto now = std::chrono::steady_clock::now();

    // Send packets in different spaces
    detector.on_packet_sent(1, 100, now, true, cnetmod::quic::pn_space::initial);
    detector.on_packet_sent(1, 100, now, true, cnetmod::quic::pn_space::handshake);
    detector.on_packet_sent(1, 100, now, true, cnetmod::quic::pn_space::application);

    // ACK only in initial space
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 1;
    ack.ack_delay = 0;
    ack.ack_range_count = 0;
    ack.first_ack_range = 0;

    auto result = detector.on_ack_received(
        ack, 1, now + 50ms, cnetmod::quic::pn_space::initial);
    ASSERT_TRUE(result.has_value());

    // Each packet-number space has independent acknowledgement and loss state.
    auto lost_initial = detector.detect_lost_packets(now + 100ms, cnetmod::quic::pn_space::initial);
    auto lost_handshake = detector.detect_lost_packets(now + 100ms, cnetmod::quic::pn_space::handshake);
    auto lost_app = detector.detect_lost_packets(now + 100ms, cnetmod::quic::pn_space::application);

    ASSERT_TRUE(lost_initial.empty());
    ASSERT_TRUE(lost_handshake.empty());
    ASSERT_TRUE(lost_app.empty());
}

// =============================================================================
// Tests: RTT sample structure
// =============================================================================

TEST(loss_detector_rtt_sample_fields)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);

    // Before initialization
    auto& rtt = detector.rtt_estimate();
    ASSERT_TRUE(rtt.smoothed_rtt_ == std::chrono::steady_clock::duration{0});
    ASSERT_TRUE(rtt.latest_rtt_ == std::chrono::steady_clock::duration{0});

    // min_rtt starts at 60s
    auto min_s = std::chrono::duration_cast<std::chrono::seconds>(rtt.min_rtt_).count();
    ASSERT_EQ(min_s, 60);
}

RUN_TESTS();
