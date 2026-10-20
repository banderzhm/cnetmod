#include "test_framework.hpp"

import std;
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

TEST(loss_detector_discarded_packet_space_clears_pto_and_bytes)
{
    auto config = make_test_config();
    cnetmod::quic::loss_detector detector(config);
    const auto now = std::chrono::steady_clock::now();

    detector.on_packet_sent(7U, 512U, now, true,
        cnetmod::quic::pn_space::initial);
    ASSERT_EQ(detector.in_flight_packet_count(cnetmod::quic::pn_space::initial),
        1U);
    ASSERT_EQ(detector.bytes_in_flight(cnetmod::quic::pn_space::initial), 512U);
    ASSERT_TRUE(detector.next_pto_deadline().has_value());

    // Once Handshake keys authenticate, Initial packets are no longer valid
    // on the wire. Their PTO state must disappear with the key material.
    ASSERT_EQ(detector.discard_packet_number_space(cnetmod::quic::pn_space::initial),
        512U);
    ASSERT_EQ(detector.in_flight_packet_count(cnetmod::quic::pn_space::initial),
        0U);
    ASSERT_EQ(detector.bytes_in_flight(cnetmod::quic::pn_space::initial), 0U);
    ASSERT_FALSE(detector.next_pto_deadline().has_value());
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

TEST(quic_recovery_controller_removes_acknowledged_pto_aliases)
{
    auto config = make_test_config();
    cnetmod::quic::quic_recovery_controller recovery(config);
    const auto now = std::chrono::steady_clock::now();
    const auto frame_bytes = std::vector<std::byte>{
        std::byte{0x08}, std::byte{0x01}, std::byte{0x00}};

    recovery.on_packet_sent(1U, 1200U, now, true,
        cnetmod::quic::pn_space::application,
        cnetmod::quic::retransmittable_frame_storage{
            cnetmod::quic::quic_send_frame{frame_bytes}});
    recovery.on_packet_sent(2U, 1200U, now + 1ms, true,
        cnetmod::quic::pn_space::application,
        cnetmod::quic::retransmittable_frame_storage{
            cnetmod::quic::quic_send_frame{frame_bytes}},
        std::nullopt, true);

    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 1U;
    auto update = recovery.on_ack_received(
        ack, now + 20ms, cnetmod::quic::pn_space::application);

    ASSERT_TRUE(update.has_value());
    ASSERT_TRUE(update->lost_frames.empty());
    ASSERT_FALSE(recovery.newest_probe_frames(
                             cnetmod::quic::pn_space::application)
            .has_value());
}

TEST(quic_recovery_controller_emits_lost_frames)
{
    auto config = make_test_config();
    cnetmod::quic::quic_recovery_controller recovery(config);
    const auto now = std::chrono::steady_clock::now();

    for (std::uint64_t packet = 1U; packet <= 4U; ++packet)
    {
        recovery.on_packet_sent(packet, 1200U, now, true,
            cnetmod::quic::pn_space::application,
            cnetmod::quic::retransmittable_frame_storage{
                cnetmod::quic::quic_send_frame{std::vector<std::byte>{
                    std::byte{0x08}, static_cast<std::byte>(packet)}}});
    }

    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 4U;
    auto update = recovery.on_ack_received(
        ack, now + 20ms, cnetmod::quic::pn_space::application);

    ASSERT_TRUE(update.has_value());
    ASSERT_EQ(update->lost_frames.size(), 1U);
    ASSERT_EQ(std::to_integer<unsigned>(
                  update->lost_frames.front().owned_bytes().back()),
        1U);
}

namespace {

auto shared_test_frames(const std::shared_ptr<const std::string>& body)
    -> cnetmod::quic::retransmittable_frame_storage
{
    const std::array header{std::byte{0x0a}, std::byte{0x01}, std::byte{0x04}};
    return cnetmod::quic::retransmittable_frame_storage{
        cnetmod::quic::quic_send_frame::shared_stream(header, body, 0U, body->size(), 1U)};
}

} // namespace

TEST(quic_recovery_shared_pto_alias_releases_body_after_original_ack)
{
    cnetmod::quic::quic_recovery_controller recovery(make_test_config());
    const auto now = std::chrono::steady_clock::now();
    const auto space = cnetmod::quic::pn_space::application;
    auto body = std::make_shared<const std::string>("body");
    const std::weak_ptr<const std::string> owner = body;
    recovery.on_packet_sent(1U, 1200U, now, true, space, shared_test_frames(body));
    body.reset();
    ASSERT_FALSE(owner.expired());
    recovery.on_pto_expired(space);
    auto probe = recovery.newest_probe_frames(space);
    ASSERT_TRUE(probe.has_value());
    if (!probe)
        return;
    recovery.on_packet_sent(2U, 1200U, now + 1ms, true, space,
        std::move(*probe), std::nullopt, true);
    probe.reset();
    ASSERT_FALSE(owner.expired());

    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 1U;
    auto update = recovery.on_ack_received(ack, now + 20ms, space);
    ASSERT_TRUE(update.has_value());
    if (!update)
        return;
    ASSERT_TRUE(update->lost_frames.empty());
    ASSERT_FALSE(recovery.newest_probe_frames(space).has_value());
    ASSERT_TRUE(owner.expired());
    // The probe's wire packet can still await its own ACK after its payload
    // ownership is released. Settle it before checking the flight timer.
    ack.largest_acked = 2U;
    update = recovery.on_ack_received(ack, now + 21ms, space);
    ASSERT_TRUE(update.has_value());
    if (!update)
        return;
    ASSERT_TRUE(update->lost_frames.empty());
    ASSERT_FALSE(recovery.next_pto_deadline().has_value());
}

TEST(quic_recovery_repeated_bursts_release_acknowledged_shared_bodies)
{
    cnetmod::quic::quic_recovery_controller recovery(make_test_config());
    const auto now = std::chrono::steady_clock::now();
    for (const auto space : {cnetmod::quic::pn_space::initial,
             cnetmod::quic::pn_space::handshake, cnetmod::quic::pn_space::application})
    {
        // More than the 64-node cache capacity exercises both cached and
        // upstream nodes again on every round, without changing ACK ranges.
        for (std::uint64_t round{}; round < 3U; ++round)
        {
            auto body = std::make_shared<const std::string>("body");
            const std::weak_ptr<const std::string> owner = body;
            const auto first = round * 96U + 1U;
            for (std::uint64_t packet = first; packet < first + 96U; ++packet)
                recovery.on_packet_sent(packet, 1200U, now, true, space,
                    shared_test_frames(body));
            body.reset();
            ASSERT_FALSE(owner.expired());
            cnetmod::quic::ack_frame ack;
            ack.largest_acked = first + 95U;
            ack.first_ack_range = 95U;
            auto update = recovery.on_ack_received(ack, now + 20ms, space);
            ASSERT_TRUE(update.has_value());
            if (!update)
                return;
            ASSERT_TRUE(update->lost_frames.empty());
            ASSERT_TRUE(owner.expired());
            ASSERT_FALSE(recovery.newest_probe_frames(space).has_value());
            ASSERT_FALSE(recovery.next_pto_deadline().has_value());
        }
        recovery.discard_packet_number_space(space);
    }
}

TEST(quic_recovery_recycled_loss_nodes_transfer_body_ownership)
{
    cnetmod::quic::quic_recovery_controller recovery(make_test_config());
    const auto now = std::chrono::steady_clock::now();
    const auto space = cnetmod::quic::pn_space::application;
    auto body = std::make_shared<const std::string>("body");
    const std::weak_ptr<const std::string> owner = body;
    recovery.on_packet_sent(1U, 1200U, now, true, space, shared_test_frames(body));
    body.reset();
    recovery.on_packet_sent(4U, 1200U, now, true, space);
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 4U;
    auto update = recovery.on_ack_received(ack, now + 20ms, space);
    ASSERT_TRUE(update.has_value());
    if (!update)
        return;
    ASSERT_EQ(update->lost_frames.size(), 1U);
    ASSERT_FALSE(owner.expired());
    // The event is now the sole owner: the recycled map node is empty.
    update->lost_frames.clear();
    ASSERT_TRUE(owner.expired());
    recovery.on_packet_sent(5U, 1200U, now + 21ms, true, space);
    ack.largest_acked = 5U;
    update = recovery.on_ack_received(ack, now + 22ms, space);
    ASSERT_TRUE(update.has_value());
    if (!update)
        return;
    ASSERT_TRUE(update->lost_frames.empty());
    ASSERT_FALSE(recovery.next_pto_deadline().has_value());
}

TEST(quic_recovery_discard_releases_shared_bodies_and_reuses_space)
{
    cnetmod::quic::quic_recovery_controller recovery(make_test_config());
    const auto now = std::chrono::steady_clock::now();
    const auto space = cnetmod::quic::pn_space::application;
    auto body = std::make_shared<const std::string>("body");
    const std::weak_ptr<const std::string> owner = body;
    for (std::uint64_t packet = 1U; packet <= 96U; ++packet)
        recovery.on_packet_sent(packet, 1200U, now, true, space, shared_test_frames(body));
    body.reset();
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 64U;
    ack.first_ack_range = 63U;
    auto update = recovery.on_ack_received(ack, now + 20ms, space);
    ASSERT_TRUE(update.has_value());
    if (!update)
        return;
    ASSERT_FALSE(owner.expired());
    recovery.discard_packet_number_space(space);
    ASSERT_TRUE(owner.expired());
    ASSERT_FALSE(recovery.next_pto_deadline().has_value());
    recovery.on_packet_sent(1U, 1200U, now + 21ms, true, space);
    ack.largest_acked = 1U;
    ack.first_ack_range = 0U;
    update = recovery.on_ack_received(ack, now + 22ms, space);
    ASSERT_TRUE(update.has_value());
    if (!update)
        return;
    ASSERT_TRUE(update->lost_frames.empty());
}

TEST(quic_recovery_copy_and_move_keep_live_packets_and_empty_source_caches)
{
    using recovery_type = cnetmod::quic::quic_recovery_controller;
    static_assert(std::is_copy_constructible_v<recovery_type>);
    static_assert(std::is_copy_assignable_v<recovery_type>);
    static_assert(std::is_move_constructible_v<recovery_type>);
    static_assert(std::is_move_assignable_v<recovery_type>);
    recovery_type original(make_test_config());
    const auto now = std::chrono::steady_clock::now();
    const auto space = cnetmod::quic::pn_space::application;
    original.on_packet_sent(1U, 1200U, now, true, space);
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 1U;
    ASSERT_TRUE(original.on_ack_received(ack, now + 20ms, space).has_value());
    // Move a populated cache, then reuse both the moved-to and moved-from
    // controllers. A stale cache count would dereference an empty node here.
    auto moved = std::move(original);
    original.on_packet_sent(2U, 1200U, now, true, space);
    ack.largest_acked = 2U;
    ASSERT_TRUE(original.on_ack_received(ack, now + 20ms, space).has_value());
    auto body = std::make_shared<const std::string>("body");
    const std::weak_ptr<const std::string> owner = body;
    moved.on_packet_sent(2U, 1200U, now, true, space, shared_test_frames(body));
    auto copied = moved;
    body.reset();
    ack.largest_acked = 2U;
    ASSERT_TRUE(moved.on_ack_received(ack, now + 20ms, space).has_value());
    ASSERT_FALSE(owner.expired());
    ASSERT_TRUE(copied.on_ack_received(ack, now + 20ms, space).has_value());
    ASSERT_TRUE(owner.expired());
    original = copied;
    original.on_packet_sent(3U, 1200U, now, true, space);
    ack.largest_acked = 3U;
    ASSERT_TRUE(original.on_ack_received(ack, now + 20ms, space).has_value());
    moved = std::move(original);
    original.on_packet_sent(4U, 1200U, now, true, space);
    ack.largest_acked = 4U;
    ASSERT_TRUE(original.on_ack_received(ack, now + 20ms, space).has_value());
}


TEST(quic_recovery_lost_pmtu_probe_preserves_window_and_releases_flight)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_test_config();
        config.congestion_algorithm = algorithm;
        cnetmod::quic::quic_recovery_controller recovery(config);
        const auto now = std::chrono::steady_clock::now();
        cnetmod::quic::quic_recovery_controller control(config);
        control.on_packet_sent(4U, 100U, now, true,
            cnetmod::quic::pn_space::application);
        recovery.on_packet_sent(1U, 1328U, now, true,
            cnetmod::quic::pn_space::application, {}, 1328U);
        recovery.on_packet_sent(4U, 100U, now, true,
            cnetmod::quic::pn_space::application);
        cnetmod::quic::ack_frame ack;
        ack.largest_acked = 4U;
        const auto update = recovery.on_ack_received(
            ack, now + 20ms, cnetmod::quic::pn_space::application);
        ASSERT_TRUE(update.has_value());
        ASSERT_EQ(update->lost_path_mtu_probes.size(), 1U);
        ASSERT_EQ(update->lost_path_mtu_probes.front(), 1328U);
        ASSERT_TRUE(update->acknowledged_path_mtu_probes.empty());
        ASSERT_TRUE(update->lost_frames.empty());
        const auto control_update = control.on_ack_received(
            ack, now + 20ms, cnetmod::quic::pn_space::application);
        ASSERT_TRUE(control_update.has_value());
        ASSERT_EQ(recovery.congestion_window(), control.congestion_window());
        ASSERT_TRUE(recovery.can_send_datagram(recovery.congestion_window()));
        ASSERT_FALSE(recovery.next_pto_deadline().has_value());
    }
}

TEST(quic_recovery_mixed_pmtu_and_ordinary_loss_still_reduces_window)
{
    auto config = make_test_config();
    config.congestion_algorithm = cnetmod::quic::quic_congestion_algorithm::new_reno;
    cnetmod::quic::quic_recovery_controller recovery(config);
    const auto now = std::chrono::steady_clock::now();
    const auto before = recovery.congestion_window();
    recovery.on_packet_sent(1U, 1328U, now, true,
        cnetmod::quic::pn_space::application, {}, 1328U);
    const std::vector frame{std::byte{0x01}};
    recovery.on_packet_sent(2U, 1200U, now, true,
        cnetmod::quic::pn_space::application,
        cnetmod::quic::retransmittable_frame_storage{
            cnetmod::quic::quic_send_frame{frame}});
    recovery.on_packet_sent(5U, 100U, now, true,
        cnetmod::quic::pn_space::application);
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 5U;
    const auto update = recovery.on_ack_received(
        ack, now + 20ms, cnetmod::quic::pn_space::application);
    ASSERT_TRUE(update.has_value());
    ASSERT_EQ(update->lost_path_mtu_probes.size(), 1U);
    ASSERT_EQ(update->lost_frames.size(), 1U);
    ASSERT_TRUE(update->lost_frames.front().owned_bytes() == frame);
    ASSERT_EQ(recovery.congestion_window(), (before + 100U) / 2U);
    ASSERT_TRUE(recovery.can_send_datagram(recovery.congestion_window()));
}

TEST(quic_recovery_pmtu_probe_pto_cleanup_preserves_window)
{
    for (const auto algorithm : {cnetmod::quic::quic_congestion_algorithm::new_reno,
             cnetmod::quic::quic_congestion_algorithm::cubic,
             cnetmod::quic::quic_congestion_algorithm::bbr})
    {
        auto config = make_test_config();
        config.congestion_algorithm = algorithm;
        cnetmod::quic::quic_recovery_controller recovery(config);
        const auto before = recovery.congestion_window();
        recovery.on_packet_sent(1U, 1328U, std::chrono::steady_clock::now(), true,
            cnetmod::quic::pn_space::application, {}, 1328U);
        ASSERT_FALSE(recovery.newest_probe_frames(
            cnetmod::quic::pn_space::application).has_value());
        const auto probes = recovery.discard_non_retransmittable(
            cnetmod::quic::pn_space::application);
        ASSERT_EQ(probes.size(), 1U);
        ASSERT_EQ(probes.front(), 1328U);
        ASSERT_EQ(recovery.congestion_window(), before);
        ASSERT_TRUE(recovery.can_send_datagram(before));
        ASSERT_FALSE(recovery.next_pto_deadline().has_value());
        ASSERT_TRUE(recovery.discard_non_retransmittable(
            cnetmod::quic::pn_space::application).empty());
        ASSERT_EQ(recovery.congestion_window(), before);
    }
}

TEST(quic_recovery_pmtu_pto_cleanup_keeps_ordinary_datagram_loss_response)
{
    auto config = make_test_config();
    config.congestion_algorithm = cnetmod::quic::quic_congestion_algorithm::new_reno;
    cnetmod::quic::quic_recovery_controller recovery(config);
    const auto before = recovery.congestion_window();
    const auto now = std::chrono::steady_clock::now();
    recovery.on_packet_sent(1U, 1328U, now, true,
        cnetmod::quic::pn_space::application, {}, 1328U);
    recovery.on_packet_sent(2U, 1200U, now, true,
        cnetmod::quic::pn_space::application);
    const auto probes = recovery.discard_non_retransmittable(
        cnetmod::quic::pn_space::application);
    ASSERT_EQ(probes.size(), 1U);
    ASSERT_EQ(recovery.congestion_window(), before / 2U);
    ASSERT_TRUE(recovery.can_send_datagram(recovery.congestion_window()));
}


TEST(quic_recovery_lost_pmtu_probe_preserves_reliable_frame_retransmission)
{
    auto config = make_test_config();
    config.congestion_algorithm = cnetmod::quic::quic_congestion_algorithm::new_reno;
    cnetmod::quic::quic_recovery_controller recovery(config);
    const auto before = recovery.congestion_window();
    const auto now = std::chrono::steady_clock::now();
    const std::vector frame{std::byte{0x0a}, std::byte{0x00}, std::byte{0x01}, std::byte{0x42}};
    recovery.on_packet_sent(1U, 1328U, now, true,
        cnetmod::quic::pn_space::application,
        cnetmod::quic::retransmittable_frame_storage{
            cnetmod::quic::quic_send_frame{frame}}, 1328U);
    recovery.on_packet_sent(4U, 100U, now, true,
        cnetmod::quic::pn_space::application);
    cnetmod::quic::ack_frame ack;
    ack.largest_acked = 4U;
    const auto update = recovery.on_ack_received(
        ack, now + 20ms, cnetmod::quic::pn_space::application);
    ASSERT_TRUE(update.has_value());
    ASSERT_EQ(update->lost_path_mtu_probes.size(), 1U);
    ASSERT_EQ(update->lost_frames.size(), 1U);
    ASSERT_TRUE(update->lost_frames.front().owned_bytes() == frame);
    ASSERT_EQ(recovery.congestion_window(), before + 100U);
    ASSERT_TRUE(recovery.can_send_datagram(recovery.congestion_window()));
    ASSERT_FALSE(recovery.next_pto_deadline().has_value());
}

RUN_TESTS();
