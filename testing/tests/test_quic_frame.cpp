#include "test_framework.hpp"

#include <array>
#include <sstream>
#include <vector>

import cnetmod.protocol.quic;
import std;
import cnetmod.protocol.quic.send_pipeline;

TEST(quic_ping_frame_roundtrip)
{
    // Test PING frame encoding and decoding
    cnetmod::quic::ping_frame original;

    auto encoded = cnetmod::quic::encode_frame(original);
    ASSERT_TRUE(encoded.size() == 1);
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x01);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::ping_frame>(result->first));
}

TEST(quic_ack_frame_with_ranges)
{
    // Test ACK frame with multiple ranges
    cnetmod::quic::ack_frame frame;
    frame.largest_acked = 100;
    frame.ack_delay = 5;
    frame.has_ecn = true;

    // Add some ack ranges
    frame.ack_range_count = 2;
    frame.first_ack_range = 5;

    cnetmod::quic::ack_range range1;
    range1.gap = 10;
    range1.ack_range_length = 4;
    frame.ack_ranges.push_back(range1);

    cnetmod::quic::ack_range range2;
    range2.gap = 3;
    range2.ack_range_length = 2;
    frame.ack_ranges.push_back(range2);

    frame.ect_0_count = 0;
    frame.ect_1_count = 0;
    frame.ecn_ce_count = 0;

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::ack_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::ack_frame>(result->first);
    ASSERT_EQ(decoded.largest_acked, frame.largest_acked);
    ASSERT_EQ(decoded.ack_delay, frame.ack_delay);
    ASSERT_EQ(decoded.ack_range_count, frame.ack_range_count);
    ASSERT_EQ(decoded.ack_ranges.size(), frame.ack_ranges.size());
}

TEST(quic_stream_frame_encoding)
{
    // Test STREAM frame with offset and data
    cnetmod::quic::stream_frame frame;
    frame.stream_id = 42;
    frame.offset = 100;
    frame.fin = false;

    // Add some test data
    std::vector<std::byte> data = {
        static_cast<std::byte>('H'),
        static_cast<std::byte>('e'),
        static_cast<std::byte>('l'),
        static_cast<std::byte>('l'),
        static_cast<std::byte>('o')};
    frame.data = std::span<const std::byte>(data.data(), data.size());

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > frame.data.size());

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::stream_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::stream_frame>(result->first);
    ASSERT_EQ(decoded.stream_id, frame.stream_id);
    ASSERT_EQ(decoded.offset, frame.offset);
    ASSERT_EQ(decoded.fin, frame.fin);
    ASSERT_EQ(decoded.data.size(), frame.data.size());
}

TEST(quic_crypto_frame_roundtrip)
{
    // Test CRYPTO frame
    cnetmod::quic::crypto_frame frame;
    frame.offset = 0;

    std::vector<std::byte> crypto_data = {
        static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
        static_cast<std::byte>(0x03), static_cast<std::byte>(0x04),
        static_cast<std::byte>(0x05)};
    frame.data = std::span<const std::byte>(crypto_data.data(), crypto_data.size());

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::crypto_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::crypto_frame>(result->first);
    ASSERT_EQ(decoded.offset, frame.offset);
    ASSERT_EQ(decoded.data.size(), frame.data.size());
}

TEST(quic_connection_close_frame_decode)
{
    // Test CONNECTION_CLOSE frame with reason text
    cnetmod::quic::connection_close_frame frame;
    frame.is_application_error = false;
    frame.error_code = 0x200;      // Protocol error
    frame.frame_type_value = 0x01; // PING frame type
    frame.reason = "Protocol violation detected";

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);
    ASSERT_TRUE(encoded.size() > frame.reason.size());

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::connection_close_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::connection_close_frame>(result->first);
    ASSERT_EQ(decoded.error_code, frame.error_code);
    ASSERT_EQ(decoded.reason, frame.reason);
}

TEST(quic_path_challenge_roundtrip)
{
    // Test PATH_CHALLENGE frame (8 bytes of random data)
    cnetmod::quic::path_challenge_frame frame;

    // Generate some test data (exactly 8 bytes)
    for (int i = 0; i < 8; ++i)
    {
        frame.data[i] = static_cast<std::byte>(i * 7 & 0xFF);
    }

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() == 9);                           // 1 byte type + 8 bytes data
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x1A); // PATH_CHALLENGE type

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::path_challenge_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::path_challenge_frame>(result->first);
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(std::to_integer<unsigned>(decoded.data[i]),
            std::to_integer<unsigned>(frame.data[i]));
    }
}

TEST(quic_unknown_frame_type_handling)
{
    // Test that unknown frame types are rejected
    // Frame type 0x1F is not defined in the spec
    std::array<std::byte, 6> unknown_frame{{static_cast<std::byte>(0x1F), // Unknown frame type
        static_cast<std::byte>(0x00),                                     // Some payload varint prefix
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00)}};

    std::span<const std::byte> input{unknown_frame.data(), unknown_frame.size()};
    auto result = cnetmod::quic::decode_frame(input);

    // Should fail to decode unknown frame type
    ASSERT_FALSE(result.has_value());
}

TEST(quic_priority_service_budget_bounds_background_starvation)
{
    cnetmod::quic::priority_service_budget budget;
    std::array<bool, 8> ready{};
    ready[0] = true;
    ready[7] = true;

    // A busy urgency-0 class still owns its configured first-packet burst,
    // but a ready urgency-7 stream must run immediately after that burst
    // rather than wait until foreground traffic happens to drain.
    for (std::size_t index{}; index < 16U; ++index)
    {
        const auto selected = budget.take_next(ready);
        ASSERT_TRUE(selected.has_value());
        ASSERT_EQ(*selected, 0U);
    }
    const auto background = budget.take_next(ready);
    ASSERT_TRUE(background.has_value());
    ASSERT_EQ(*background, 7U);

    // Exhausting every currently ready class starts a new epoch; it does not
    // make a live stream permanently unschedulable.
    ready[7] = false;
    for (std::size_t index{}; index < 16U; ++index)
        ASSERT_TRUE(budget.take_next(ready).has_value());
    const auto next_epoch = budget.take_next(ready);
    ASSERT_TRUE(next_epoch.has_value());
    ASSERT_EQ(*next_epoch, 0U);
}

TEST(quic_shared_send_frame_renders_without_owning_body_copy)
{
    const auto body = std::make_shared<const std::string>("body");
    const std::array header{std::byte{0x0a}, std::byte{0x04}, std::byte{0x04}};
    const auto frame = cnetmod::quic::quic_send_frame::shared_stream(
        header, body, 0U, body->size(), 4U);
    const auto alias = frame;

    std::vector<std::byte> encoded;
    frame.append_to(encoded);
    const auto decoded = cnetmod::quic::decode_frame(encoded);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::stream_frame>(decoded->first));
    const auto& stream = std::get<cnetmod::quic::stream_frame>(decoded->first);
    ASSERT_EQ(stream.stream_id, 4U);
    ASSERT_EQ(stream.data.size(), body->size());
    ASSERT_TRUE(frame == alias);
}

TEST(quic_stream_scheduler_preserves_default_fifo_across_payload_strategies)
{
    cnetmod::quic::quic_stream_scheduler scheduler;
    scheduler.enqueue(0U,
        cnetmod::quic::quic_send_frame::owned({std::byte{0x01}}, 0U));
    const auto body = std::make_shared<const std::string>("x");
    const std::array header{std::byte{0x0a}, std::byte{0x04}, std::byte{0x01}};
    scheduler.enqueue(4U, cnetmod::quic::quic_send_frame::shared_stream(
                              header, body, 0U, 1U, 4U));

    const auto first = scheduler.take();
    const auto second = scheduler.take();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first->stream, 0U);
    ASSERT_EQ(second->stream, 4U);
    ASSERT_TRUE(scheduler.empty());
}

TEST(quic_empty_stream_frame)
{
    // Test empty stream frame (no data)
    cnetmod::quic::stream_frame frame;
    frame.stream_id = 8;
    frame.offset = 0;
    frame.fin = true; // Even without data, can have FIN
    frame.data = {};

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Should still encode successfully even with no data
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::stream_frame>(result->first));
}

TEST(quic_large_ack_ranges_list)
{
    // Test ACK frame with maximum recommended range count (RFC 9000 recommends max 64)
    cnetmod::quic::ack_frame frame;
    frame.largest_acked = 1000;
    frame.ack_delay = 10;
    frame.has_ecn = false;
    frame.ack_range_count = 10; // Test with a reasonable number

    // Create multiple ack ranges
    for (std::uint64_t i = 0; i < frame.ack_range_count; ++i)
    {
        cnetmod::quic::ack_range range;
        range.gap = 1;
        range.ack_range_length = 1;
        frame.ack_ranges.push_back(range);
    }

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());

    auto& decoded = std::get<cnetmod::quic::ack_frame>(result->first);
    ASSERT_EQ(decoded.ack_ranges.size(), frame.ack_ranges.size());

    for (size_t i = 0; i < frame.ack_ranges.size(); ++i)
    {
        ASSERT_EQ(decoded.ack_ranges[i].gap, frame.ack_ranges[i].gap);
        ASSERT_EQ(decoded.ack_ranges[i].ack_range_length, frame.ack_ranges[i].ack_range_length);
    }
}

TEST(quic_max_data_frame_encoding)
{
    // Test MAX_DATA frame
    cnetmod::quic::max_data_frame frame;
    frame.maximum = 1048576; // 1MB

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::max_data_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::max_data_frame>(result->first);
    ASSERT_EQ(decoded.maximum, frame.maximum);
}

TEST(quic_new_connection_id_frame_encoding)
{
    // Test NEW_CONNECTION_ID frame
    cnetmod::quic::new_connection_id_frame frame;
    frame.sequence_number = 1;
    frame.retire_prior_to = 1;
    std::array<std::byte, 4> cid_bytes{};
    for (std::size_t i = 0; i < cid_bytes.size(); ++i)
    {
        cid_bytes[i] = static_cast<std::byte>(i + 1);
    }
    frame.cid = cnetmod::quic::connection_id{cid_bytes.data(),
        static_cast<std::uint8_t>(cid_bytes.size())};

    // Fill stateless reset token
    for (int i = 0; i < 16; ++i)
    {
        frame.stateless_reset_token[i] = static_cast<std::byte>((i * 3) & 0xFF);
    }

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::new_connection_id_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::new_connection_id_frame>(result->first);
    ASSERT_EQ(decoded.sequence_number, frame.sequence_number);
    ASSERT_EQ(decoded.cid.size(), frame.cid.size());
}

TEST(quic_padding_frame_encode)
{
    // PADDING frame encodes to a single 0x00 byte
    cnetmod::quic::padding_frame frame;
    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_EQ(encoded.size(), 1u);
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x00);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::padding_frame>(result->first));
}

TEST(quic_stream_frame_fin_flag_roundtrip)
{
    // STREAM frame with FIN flag set
    cnetmod::quic::stream_frame frame;
    frame.stream_id = 0x1234;
    frame.offset = 0;
    frame.fin = true;

    std::vector<std::byte> stream_data(64);
    std::fill(stream_data.begin(), stream_data.end(), std::byte{0xAA});
    frame.data = std::span<const std::byte>(stream_data.data(), stream_data.size());

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::stream_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::stream_frame>(result->first);
    ASSERT_EQ(decoded.stream_id, 0x1234ULL);
    ASSERT_TRUE(decoded.fin);
    ASSERT_EQ(decoded.data.size(), 64u);
}

TEST(quic_crypto_frame_large_offset)
{
    // CRYPTO frame with large offset (simulating TLS handshake continuation)
    cnetmod::quic::crypto_frame frame;
    frame.offset = 1024;

    std::vector<std::byte> crypto_data(256);
    for (std::size_t i = 0; i < crypto_data.size(); ++i)
    {
        crypto_data[i] = static_cast<std::byte>(i & 0xFF);
    }
    frame.data = std::span<const std::byte>(crypto_data.data(), crypto_data.size());

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::crypto_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::crypto_frame>(result->first);
    ASSERT_EQ(decoded.offset, 1024ULL);
    ASSERT_EQ(decoded.data.size(), 256u);
}

TEST(quic_handshake_done_frame_roundtrip)
{
    // HANDSHAKE_DONE frame (type 0x1E)
    cnetmod::quic::handshake_done_frame frame;

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() == 1);
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x1E);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::handshake_done_frame>(result->first));
}

TEST(quic_reset_stream_frame_roundtrip)
{
    // RESET_STREAM frame
    cnetmod::quic::reset_stream_frame frame;
    frame.stream_id = 42;
    frame.application_error_code = 0x100;
    frame.final_size = 4096;

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::reset_stream_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::reset_stream_frame>(result->first);
    ASSERT_EQ(decoded.stream_id, 42ULL);
    ASSERT_EQ(decoded.application_error_code, 0x100ULL);
    ASSERT_EQ(decoded.final_size, 4096ULL);
}

TEST(quic_max_stream_data_frame_roundtrip)
{
    // MAX_STREAM_DATA frame
    cnetmod::quic::max_stream_data_frame frame;
    frame.stream_id = 8;
    frame.maximum = 1048576; // 1MB

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::max_stream_data_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::max_stream_data_frame>(result->first);
    ASSERT_EQ(decoded.stream_id, 8ULL);
    ASSERT_EQ(decoded.maximum, 1048576ULL);
}

TEST(quic_frame_decode_truncated_data)
{
    // Decoding truncated data (just type byte, no payload for frames that need it)
    // ACK frame type (0x02) with no data following
    std::array<std::byte, 1> truncated{{static_cast<std::byte>(0x02)}};

    std::span<const std::byte> input{truncated.data(), truncated.size()};
    auto result = cnetmod::quic::decode_frame(input);
    // Should fail because ACK needs additional varint fields
    ASSERT_FALSE(result.has_value());
}

TEST(quic_frame_ack_eliciting_check)
{
    // PING is ack-eliciting
    cnetmod::quic::quic_frame_variant ping_variant = cnetmod::quic::ping_frame{};
    ASSERT_TRUE(cnetmod::quic::is_ack_eliciting(ping_variant));

    // PADDING is NOT ack-eliciting
    cnetmod::quic::quic_frame_variant padding_variant = cnetmod::quic::padding_frame{};
    ASSERT_FALSE(cnetmod::quic::is_ack_eliciting(padding_variant));

    // ACK is NOT ack-eliciting
    cnetmod::quic::quic_frame_variant ack_variant = cnetmod::quic::ack_frame{};
    ASSERT_FALSE(cnetmod::quic::is_ack_eliciting(ack_variant));

    // STREAM is ack-eliciting
    cnetmod::quic::quic_frame_variant stream_variant = cnetmod::quic::stream_frame{};
    ASSERT_TRUE(cnetmod::quic::is_ack_eliciting(stream_variant));
}

TEST(quic_frame_probing_check)
{
    // PATH_CHALLENGE is probing
    cnetmod::quic::quic_frame_variant path_challenge_variant = cnetmod::quic::path_challenge_frame{};
    ASSERT_TRUE(cnetmod::quic::is_probing(path_challenge_variant));

    // PATH_RESPONSE is probing
    cnetmod::quic::quic_frame_variant path_response_variant = cnetmod::quic::path_response_frame{};
    ASSERT_TRUE(cnetmod::quic::is_probing(path_response_variant));

    // PING is NOT probing
    cnetmod::quic::quic_frame_variant ping_variant = cnetmod::quic::ping_frame{};
    ASSERT_FALSE(cnetmod::quic::is_probing(ping_variant));

    // RFC 9000 section 9.1: PADDING is permitted in a probing packet.
    cnetmod::quic::quic_frame_variant padding_variant = cnetmod::quic::padding_frame{};
    ASSERT_TRUE(cnetmod::quic::is_probing(padding_variant));
}

TEST(quic_stop_sending_frame_roundtrip)
{
    // STOP_SENDING frame
    cnetmod::quic::stop_sending_frame frame;
    frame.stream_id = 16;
    frame.application_error_code = 0x42;

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::stop_sending_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::stop_sending_frame>(result->first);
    ASSERT_EQ(decoded.stream_id, 16ULL);
    ASSERT_EQ(decoded.application_error_code, 0x42ULL);
}

TEST(quic_data_blocked_frame_roundtrip)
{
    // DATA_BLOCKED frame
    cnetmod::quic::data_blocked_frame frame;
    frame.maximum_data = 65536;

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::data_blocked_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::data_blocked_frame>(result->first);
    ASSERT_EQ(decoded.maximum_data, 65536ULL);
}

TEST(quic_retire_connection_id_frame_roundtrip)
{
    // RETIRE_CONNECTION_ID frame
    cnetmod::quic::retire_connection_id_frame frame;
    frame.sequence_number = 5;

    auto encoded = cnetmod::quic::encode_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::quic::decode_frame(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::retire_connection_id_frame>(result->first));

    auto& decoded = std::get<cnetmod::quic::retire_connection_id_frame>(result->first);
    ASSERT_EQ(decoded.sequence_number, 5ULL);
}

TEST(quic_datagram_frame_roundtrip)
{
    const std::array bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    cnetmod::quic::datagram_frame frame{bytes, true};

    auto encoded = cnetmod::quic::encode_frame(frame);
    auto result = cnetmod::quic::decode_frame(encoded);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::datagram_frame>(result->first));
    const auto& decoded = std::get<cnetmod::quic::datagram_frame>(result->first);
    ASSERT_TRUE(decoded.has_length);
    ASSERT_EQ(decoded.data.size(), bytes.size());
    ASSERT_EQ(std::to_integer<std::uint8_t>(decoded.data[2]), 0x03U);
}

TEST(quic_datagram_frame_without_length_consumes_packet_tail)
{
    const std::array bytes{std::byte{0x2a}, std::byte{0x2b}};
    cnetmod::quic::datagram_frame frame{bytes, false};

    auto encoded = cnetmod::quic::encode_frame(frame);
    auto result = cnetmod::quic::decode_frame(encoded);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->second, encoded.size());
    const auto& decoded = std::get<cnetmod::quic::datagram_frame>(result->first);
    ASSERT_FALSE(decoded.has_length);
    ASSERT_EQ(decoded.data.size(), bytes.size());
}

TEST(multipath_path_ack_frame_roundtrip)
{
    cnetmod::quic::path_ack_frame frame{};
    frame.path_id = 3U;
    frame.acknowledgment.largest_acked = 42U;
    frame.acknowledgment.ack_delay = 7U;
    frame.acknowledgment.ack_range_count = 1U;
    frame.acknowledgment.first_ack_range = 3U;
    frame.acknowledgment.ack_ranges.push_back({2U, 5U});
    frame.acknowledgment.has_ecn = true;
    frame.acknowledgment.ect_0_count = 11U;
    frame.acknowledgment.ect_1_count = 12U;
    frame.acknowledgment.ecn_ce_count = 13U;

    const auto encoded = cnetmod::quic::encode_frame(frame);
    const auto decoded = cnetmod::quic::decode_frame(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::path_ack_frame>(decoded->first));
    const auto& result = std::get<cnetmod::quic::path_ack_frame>(decoded->first);
    ASSERT_EQ(result.path_id, 3U);
    ASSERT_EQ(result.acknowledgment.largest_acked, 42ULL);
    ASSERT_TRUE(result.acknowledgment.has_ecn);
    ASSERT_EQ(result.acknowledgment.ecn_ce_count, 13ULL);
}

TEST(multipath_path_management_frames_roundtrip)
{
    const cnetmod::quic::path_status_frame status{.path_id = 4U,
        .sequence_number = 9U,
        .backup = true};
    const auto status_result = cnetmod::quic::decode_frame(
        cnetmod::quic::encode_frame(status));
    ASSERT_TRUE(status_result.has_value());
    const auto& decoded_status =
        std::get<cnetmod::quic::path_status_frame>(status_result->first);
    ASSERT_EQ(decoded_status.path_id, 4U);
    ASSERT_EQ(decoded_status.sequence_number, 9ULL);
    ASSERT_TRUE(decoded_status.backup);

    const cnetmod::quic::max_path_id_frame maximum{.maximum_path_id = 0xffffffffU};
    const auto maximum_result = cnetmod::quic::decode_frame(
        cnetmod::quic::encode_frame(maximum));
    ASSERT_TRUE(maximum_result.has_value());
    ASSERT_EQ(std::get<cnetmod::quic::max_path_id_frame>(maximum_result->first)
                  .maximum_path_id,
        0xffffffffU);
}

TEST(multipath_path_connection_id_frames_roundtrip)
{
    const std::array cid_bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
        std::byte{0x04}};
    cnetmod::quic::path_new_connection_id_frame frame{};
    frame.path_id = 2U;
    frame.connection_id.sequence_number = 6U;
    frame.connection_id.retire_prior_to = 1U;
    frame.connection_id.cid = cnetmod::quic::connection_id{cid_bytes};
    frame.connection_id.stateless_reset_token[0] = std::byte{0xaa};

    const auto decoded = cnetmod::quic::decode_frame(cnetmod::quic::encode_frame(frame));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::quic::path_new_connection_id_frame>(
        decoded->first));
    const auto& result =
        std::get<cnetmod::quic::path_new_connection_id_frame>(decoded->first);
    ASSERT_EQ(result.path_id, 2U);
    ASSERT_EQ(result.connection_id.sequence_number, 6ULL);
    ASSERT_EQ(result.connection_id.cid.size(), 4U);
    ASSERT_EQ(std::to_integer<unsigned>(result.connection_id.stateless_reset_token[0]), 0xaaU);
}

TEST(multipath_frames_reject_path_ids_above_the_nonce_limit)
{
    // MAX_PATH_ID's experimental type is a four-byte QUIC varint, followed by
    // an eight-byte value encoding 2^32. Path IDs are constrained by the
    // 32-bit nonce prefix and therefore this frame is malformed.
    const std::vector<std::byte> encoded{
        std::byte{0x95}, std::byte{0x22}, std::byte{0x8c}, std::byte{0x0c},
        std::byte{0xc0}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    ASSERT_FALSE(cnetmod::quic::decode_frame(encoded).has_value());
}

TEST(quic_scheduler_retirement_reclaims_priority_but_preserves_retransmission)
{
    using namespace cnetmod::quic;
    quic_stream_scheduler scheduler;
    scheduler.set_priority(0U, 1U, true);
    scheduler.enqueue(0U, quic_send_frame::owned({std::byte{1}}, 0U));
    auto frame = scheduler.take();
    ASSERT_TRUE(frame.has_value());
    scheduler.set_priority(0U, 6U, false);
    ASSERT_TRUE(scheduler.requeue_retransmission(frame->frame));
    frame = scheduler.take();
    ASSERT_EQ(frame->urgency, 6U);
    const auto alias = frame->frame;
    scheduler.retire_stream(0U);
    ASSERT_EQ(scheduler.priority_count(), 0U);
    ASSERT_TRUE(scheduler.requeue_retransmission(frame->frame));
    frame = scheduler.take();
    ASSERT_EQ(frame->urgency, 6U);
    ASSERT_TRUE(frame->frame == alias);
    for (std::uint64_t id = 4U; id < 400004U; id += 4U)
    {
        scheduler.set_priority(id, 0U, false);
        scheduler.retire_stream(id);
    }
    ASSERT_EQ(scheduler.priority_count(), 0U);
}

TEST(quic_mixed_send_frame_matches_public_stream_encoding)
{
    using namespace cnetmod::quic;
    const std::array prefix{std::byte{0x00}, std::byte{0xff}};
    for (const auto body_size : std::array<std::size_t, 7>{1U, 61U, 62U, 63U, 64U, 4096U, 65500U})
    {
        const auto body = std::make_shared<const std::string>(body_size + 6U, 'b');
        const auto body_bytes = std::as_bytes(std::span{body->data(), body->size()});
        std::vector<std::byte> payload(prefix.begin(), prefix.end());
        payload.insert(payload.end(), body_bytes.begin() + 3U,
            body_bytes.begin() + 3U + body_size);
        for (const auto offset : {0U, 16384U})
        {
            for (const auto fin : {false, true})
            {
                // The public encoder supplies an independent canonical header.
                const auto expected = encode_frame(stream_frame{64U, offset, payload, fin});
                const auto header_size = expected.size() - payload.size();
                auto frame = quic_send_frame::shared_stream_with_prefix(
                    std::span{expected}.first(header_size), prefix,
                    body, 3U, body_size, 64U);
                ASSERT_EQ(frame.size(), expected.size());
                ASSERT_FALSE(frame.empty());
                ASSERT_FALSE(frame.is_connection_close());
                ASSERT_TRUE(frame.owned_bytes().empty());
                ASSERT_TRUE(frame.release_owned_bytes().empty());
                std::vector<std::byte> wire;
                frame.append_to(wire);
                ASSERT_TRUE(wire == expected);
                const auto decoded = decode_frame(wire);
                ASSERT_TRUE(decoded.has_value());
                if (!decoded || !std::holds_alternative<stream_frame>(decoded->first))
                {
                    ASSERT_TRUE(false);
                    return;
                }
                const auto& stream = std::get<stream_frame>(decoded->first);
                ASSERT_EQ(stream.stream_id, 64U);
                ASSERT_EQ(stream.offset, offset);
                ASSERT_EQ(stream.fin, fin);
                ASSERT_TRUE(std::ranges::equal(stream.data, payload));
            }
        }
    }
}

TEST(quic_mixed_send_frame_prefix_participates_in_alias_equality)
{
    using namespace cnetmod::quic;
    const auto body = std::make_shared<const std::string>("body");
    const std::array header{std::byte{0x0b}, std::byte{0x00}, std::byte{0x06}};
    const std::array first_prefix{std::byte{'a'}, std::byte{'b'}};
    const std::array second_prefix{std::byte{'a'}, std::byte{'c'}};
    const auto first = quic_send_frame::shared_stream_with_prefix(
        header, first_prefix, body, 0U, body->size(), 0U);
    const auto alias = first;
    const auto different = quic_send_frame::shared_stream_with_prefix(
        header, second_prefix, body, 0U, body->size(), 0U);
    ASSERT_TRUE(first == alias);
    ASSERT_FALSE(first == different);
}

TEST(quic_mixed_send_frame_copy_move_clear_retains_only_live_owners)
{
    using namespace cnetmod::quic;
    auto body = std::make_shared<const std::string>("body");
    const std::weak_ptr<const std::string> weak = body;
    const std::array header{std::byte{0x0b}, std::byte{0x00}, std::byte{0x06}};
    const std::array prefix{std::byte{'a'}, std::byte{'b'}};
    auto frame = quic_send_frame::shared_stream_with_prefix(
        header, prefix, body, 0U, body->size(), 0U);
    auto alias = frame;
    auto moved = std::move(alias);
    body.reset();
    frame.clear();
    ASSERT_FALSE(weak.expired());
    std::vector<std::byte> wire;
    moved.append_to(wire);
    const auto decoded = decode_frame(wire);
    ASSERT_TRUE(decoded.has_value());
    moved.clear();
    ASSERT_TRUE(weak.expired());
    ASSERT_TRUE(moved.empty());
    ASSERT_EQ(moved.size(), 0U);
}

TEST(quic_shared_strategy_never_exports_partial_owned_prefix_or_close)
{
    using namespace cnetmod::quic;
    const auto body = std::make_shared<const std::string>("body");
    // The raw-header factory accepts bytes; shared ownership must exclude CLOSE
    // independently of any accidentally CLOSE-looking first byte.
    const std::array header{std::byte{0x1c}};
    const std::array prefix{std::byte{'a'}};
    auto mixed = quic_send_frame::shared_stream_with_prefix(
        header, prefix, body, 0U, body->size(), 0U);
    ASSERT_FALSE(mixed.is_connection_close());
    ASSERT_TRUE(mixed.owned_bytes().empty());
    ASSERT_TRUE(mixed.release_owned_bytes().empty());
    std::vector<std::byte> wire;
    mixed.append_to(wire);
    ASSERT_EQ(wire.size(), header.size() + prefix.size() + body->size());
    const std::vector original{std::byte{0x1c}, std::byte{0x00}};
    auto owned = quic_send_frame::owned(original);
    ASSERT_TRUE(owned.is_connection_close());
    owned.mark_early_data();
    ASSERT_TRUE(owned.is_early_data());
    ASSERT_TRUE(owned.owned_bytes() == original);
    ASSERT_TRUE(owned.release_owned_bytes() == original);
}

TEST(quic_mixed_send_frame_owns_temporary_header_and_prefix)
{
    using namespace cnetmod::quic;
    auto body = std::make_shared<const std::string>("body");
    const std::weak_ptr<const std::string> owner = body;
    quic_send_frame frame;
    {
        std::vector header{std::byte{0x0b}, std::byte{0x00}, std::byte{0x06}};
        std::vector prefix{std::byte{'D'}, std::byte{'A'}};
        frame = quic_send_frame::shared_stream_with_prefix(
            header, prefix, body, 0U, body->size(), 0U);
        std::ranges::fill(header, std::byte{0x00});
        std::ranges::fill(prefix, std::byte{0x00});
    }
    body.reset();
    ASSERT_FALSE(owner.expired());
    std::vector<std::byte> wire;
    frame.append_to(wire);
    const std::vector expected{std::byte{0x0b}, std::byte{0x00}, std::byte{0x06},
        std::byte{'D'}, std::byte{'A'}, std::byte{'b'}, std::byte{'o'},
        std::byte{'d'}, std::byte{'y'}};
    ASSERT_TRUE(wire == expected);
    frame.clear();
    ASSERT_TRUE(owner.expired());
}

RUN_TESTS();

namespace {
auto pending_test_header(std::span<std::byte> output, std::uint64_t stream,
    std::uint64_t offset, std::size_t count, bool fin) -> std::size_t
{
    std::size_t used{};
    const auto append = [&](std::uint64_t value)
    {
        const auto encoded = cnetmod::quic::encode_varint_to(value, output.subspan(used));
        if (!encoded) return false;
        used += *encoded;
        return true;
    };
    if (!append(0x0aU | (offset != 0U ? 0x04U : 0U) | (fin ? 0x01U : 0U)) ||
        !append(stream) || (offset != 0U && !append(offset)) || !append(count))
        return 0U;
    return used;
}
auto pending_test_payload(const cnetmod::quic::quic_send_frame& frame)
    -> std::vector<std::byte>
{
    std::vector<std::byte> bytes;
    frame.append_to(bytes);
    return bytes;
}
} // namespace

TEST(quic_pending_range_exact_budget_prefix_body_fin_and_varint_offsets)
{
    for (const auto stream : {63U, 64U})
    for (const auto offset : {0U, 63U, 64U, 16383U, 16384U})
    {
        cnetmod::quic::quic_stream_scheduler scheduler;
        const std::vector<std::byte> prefix(9U, std::byte{0x70});
        const auto body = std::make_shared<const std::string>(130U, 'b');
        scheduler.enqueue_shared_write(stream, prefix, body, offset, true, 96U, pending_test_header);
        std::vector<std::byte> received;
        bool received_fin{};
        std::size_t fragments{};
        while (!scheduler.empty())
        {
            scheduler.begin_packet_selection();
            const auto selected = scheduler.take_selected_frame(32U, 32U);
            ASSERT_TRUE(selected.has_value());
            const auto bytes = pending_test_payload(selected->frame);
            ASSERT_TRUE(bytes.size() <= 32U);
            const auto decoded = cnetmod::quic::decode_frame(bytes);
            ASSERT_TRUE(decoded.has_value());
            ASSERT_TRUE(std::holds_alternative<cnetmod::quic::stream_frame>(decoded->first));
            const auto frame = std::get<cnetmod::quic::stream_frame>(decoded->first);
            ASSERT_EQ(frame.stream_id, stream);
            ASSERT_EQ(frame.offset, offset + received.size());
            ASSERT_FALSE(received_fin);
            received.insert(received.end(), frame.data.begin(), frame.data.end());
            received_fin = frame.fin;
            scheduler.finish_packet_selection(true);
            ASSERT_TRUE(++fragments < 64U);
        }
        std::vector<std::byte> expected = prefix;
        const auto body_bytes = std::as_bytes(std::span{body->data(), body->size()});
        expected.insert(expected.end(), body_bytes.begin(), body_bytes.end());
        ASSERT_TRUE(received == expected);
        ASSERT_TRUE(received_fin);
        ASSERT_FALSE(scheduler.has_pending_writes());
    }
}

TEST(quic_pending_range_rollback_restores_cursor_fin_and_original_ready_turn)
{
    cnetmod::quic::quic_stream_scheduler scheduler;
    scheduler.set_priority(0U, 0U, true);
    scheduler.set_priority(4U, 0U, true);
    const auto body = std::make_shared<const std::string>(256U, 'a');
    scheduler.enqueue_shared_write(0U, {}, body, 0U, false, 64U, pending_test_header);
    scheduler.enqueue_shared_write(0U, {}, body, 256U, true, 64U, pending_test_header);
    scheduler.enqueue_shared_write(4U, {}, body, 0U, true, 64U, pending_test_header);
    scheduler.begin_packet_selection();
    auto first = scheduler.take_selected_frame(16U, 16U);
    auto second = scheduler.take_selected_frame(16U, 16U);
    ASSERT_TRUE(first.has_value()); ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first->stream, 0U); ASSERT_EQ(second->stream, 0U);
    const auto original = pending_test_payload(first->frame);
    scheduler.rollback_selected_frame(std::move(*second));
    scheduler.rollback_selected_frame(std::move(*first));
    scheduler.finish_packet_selection(false);
    scheduler.begin_packet_selection();
    auto retried = scheduler.take_selected_frame(16U, 16U);
    ASSERT_TRUE(retried.has_value()); ASSERT_EQ(retried->stream, 0U);
    ASSERT_TRUE(original == pending_test_payload(retried->frame));
    scheduler.finish_packet_selection(true);
}

TEST(quic_pending_range_two_incremental_streams_keep_chunk_turn_and_background_bound)
{
    cnetmod::quic::quic_stream_scheduler scheduler;
    scheduler.set_priority(0U, 0U, true);
    scheduler.set_priority(4U, 0U, true);
    scheduler.set_priority(8U, 7U, true);
    const auto body = std::make_shared<const std::string>(4096U, 'x');
    for (const auto stream : {0U, 4U, 8U})
        scheduler.enqueue_shared_write(stream, {}, body, 0U, false, 64U, pending_test_header);
    bool saw_second{};
    std::size_t first_chunk_bytes{};
    for (std::size_t service{}; service < 16U; ++service)
    {
        scheduler.begin_packet_selection();
        const auto selected = scheduler.take_selected_frame(16U, 16U);
        ASSERT_TRUE(selected.has_value()); ASSERT_EQ(selected->urgency, 0U);
        if (selected->stream == 4U)
        {
            if (!saw_second)
                ASSERT_EQ(first_chunk_bytes, 60U);
            saw_second = true;
        }
        if (!saw_second)
        {
            const auto bytes = pending_test_payload(selected->frame);
            const auto decoded = cnetmod::quic::decode_frame(bytes);
            ASSERT_TRUE(decoded.has_value());
            first_chunk_bytes += std::get<cnetmod::quic::stream_frame>(decoded->first).data.size();
        }
        scheduler.finish_packet_selection(true);
    }
    ASSERT_TRUE(saw_second);
    scheduler.begin_packet_selection();
    const auto background = scheduler.take_selected_frame(16U, 16U);
    ASSERT_TRUE(background.has_value()); ASSERT_EQ(background->stream, 8U);
    scheduler.finish_packet_selection(true);
}

TEST(quic_pending_range_priority_migrations_and_retire_keep_owner_and_offset)
{
    cnetmod::quic::quic_stream_scheduler scheduler;
    auto body = std::make_shared<const std::string>(64U, 'r');
    std::weak_ptr<const std::string> retained = body;
    scheduler.enqueue_shared_write(4U, {std::byte{0x70}}, body, 100U, true, 64U, pending_test_header);
    body.reset();
    scheduler.set_priority(4U, 0U, false);
    scheduler.set_priority(4U, 0U, true);
    scheduler.set_priority(4U, 7U, true);
    scheduler.set_priority(4U, 3U, false);
    scheduler.set_priority(4U, 0U, true);
    scheduler.retire_stream(4U);
    ASSERT_FALSE(retained.expired());
    std::size_t received{};
    while (!scheduler.empty())
    {
        scheduler.begin_packet_selection();
        {
            const auto frame = scheduler.take_selected_frame(16U, 16U);
            ASSERT_TRUE(frame.has_value()); ASSERT_EQ(frame->urgency, 0U);
            const auto bytes = pending_test_payload(frame->frame);
            const auto decoded = cnetmod::quic::decode_frame(bytes);
            ASSERT_TRUE(decoded.has_value());
            const auto stream = std::get<cnetmod::quic::stream_frame>(decoded->first);
            ASSERT_EQ(stream.offset, 100U + received);
            received += stream.data.size();
            scheduler.finish_packet_selection(true);
        }
    }
    ASSERT_EQ(received, 65U);
    ASSERT_TRUE(retained.expired());
}

TEST(quic_pending_range_zero_credit_zero_fin_and_in_place_fallback)
{
    cnetmod::quic::quic_stream_scheduler scheduler;
    scheduler.enqueue_shared_write(4U, {}, std::make_shared<const std::string>(),
        64U, true, 64U, pending_test_header);
    scheduler.begin_packet_selection();
    ASSERT_FALSE(scheduler.take_selected_frame(64U, 0U).has_value());
    scheduler.finish_packet_selection(false);
    // Legacy take remains at the same scheduler node; no encoded FIFO diversion.
    const auto fin = scheduler.take();
    ASSERT_TRUE(fin.has_value());
    const auto bytes = pending_test_payload(fin->frame);
    const auto decoded = cnetmod::quic::decode_frame(bytes);
    ASSERT_TRUE(decoded.has_value());
    const auto stream = std::get<cnetmod::quic::stream_frame>(decoded->first);
    ASSERT_TRUE(stream.fin); ASSERT_EQ(stream.offset, 64U); ASSERT_TRUE(stream.data.empty());
    ASSERT_TRUE(scheduler.empty()); ASSERT_FALSE(scheduler.has_pending_writes());
}

TEST(quic_pending_range_committed_progress_exceeds_batch_and_blocked_stops)
{
    cnetmod::quic::quic_stream_scheduler scheduler;
    scheduler.enqueue_shared_write(0U, {}, std::make_shared<const std::string>(4096U, 'x'),
        0U, true, 1200U, pending_test_header);
    const auto initial = scheduler.pending_progress();
    for (std::size_t batch{}; batch < 2U; ++batch)
    {
        const auto before = scheduler.pending_progress();
        for (std::size_t packet{}; packet < 16U; ++packet)
        {
            scheduler.begin_packet_selection();
            auto selected = scheduler.take_selected_frame(32U, 32U);
            ASSERT_TRUE(selected.has_value());
            scheduler.finish_packet_selection(true);
        }
        ASSERT_EQ(scheduler.size(), 1U);
        ASSERT_EQ(scheduler.pending_progress(), before + 16U);
    }
    ASSERT_EQ(scheduler.pending_progress(), initial + 32U);
    const auto before = scheduler.pending_progress();
    scheduler.begin_packet_selection();
    auto selected = scheduler.take_selected_frame(32U, 32U);
    ASSERT_TRUE(selected.has_value());
    scheduler.rollback_selected_frame(std::move(*selected));
    scheduler.finish_packet_selection(false);
    ASSERT_EQ(scheduler.pending_progress(), before);
    // This is exactly the no-progress decision used by flush_send_queue:
    // unchanged logical queue size with zero credit must break after one batch.
    const auto queued = scheduler.size();
    scheduler.begin_packet_selection();
    ASSERT_FALSE(scheduler.take_selected_frame(32U, 0U).has_value());
    scheduler.finish_packet_selection(false);
    ASSERT_TRUE(scheduler.size() >= queued && scheduler.pending_progress() == before);
}

TEST(quic_pending_range_maximal_fit_at_length_varint_boundaries)
{
    for (const std::size_t target : {60U, 63U, 64U, 16383U, 16384U})
    {
        std::array<std::byte, 32> header{};
        const auto wire = pending_test_header(header, 0U, 0U, target, false) + target;
        // Legacy chunk retains the original conservative offset byte at zero.
        cnetmod::quic::quic_stream_scheduler legacy;
        legacy.enqueue_shared_write(0U, {}, std::make_shared<const std::string>(32768U, 'x'),
            0U, false, wire + 1U, pending_test_header);
        const auto whole = legacy.take();
        ASSERT_TRUE(whole.has_value());
        auto bytes = pending_test_payload(whole->frame);
        auto decoded = cnetmod::quic::decode_frame(bytes);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_EQ(std::get<cnetmod::quic::stream_frame>(decoded->first).data.size(), target);
        cnetmod::quic::quic_stream_scheduler credit;
        credit.enqueue_shared_write(0U, {}, std::make_shared<const std::string>(32768U, 'x'),
            0U, false, 32768U, pending_test_header);
        credit.begin_packet_selection();
        const auto fragment = credit.take_selected_frame(wire, wire);
        ASSERT_TRUE(fragment.has_value());
        bytes = pending_test_payload(fragment->frame);
        decoded = cnetmod::quic::decode_frame(bytes);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_EQ(std::get<cnetmod::quic::stream_frame>(decoded->first).data.size(), target);
        ASSERT_EQ(bytes.size(), wire);
        credit.finish_packet_selection(true);
    }
}

TEST(quic_pending_range_moved_prefix_survives_producer_reuse_and_rollback)
{
    cnetmod::quic::quic_stream_scheduler scheduler;
    std::vector<std::byte> prefix(9U, std::byte{0x70});
    auto body = std::make_shared<const std::string>(130U, 'b');
    const std::weak_ptr<const std::string> owner = body;
    scheduler.enqueue_shared_write(4U, std::move(prefix), body, 64U, true,
        96U, pending_test_header);
    body.reset();
    prefix.assign(4096U, std::byte{0xff});
    ASSERT_FALSE(owner.expired());
    scheduler.begin_packet_selection();
    auto rolled_back = scheduler.take_selected_frame(32U, 32U);
    ASSERT_TRUE(rolled_back.has_value());
    scheduler.rollback_selected_frame(std::move(*rolled_back));
    rolled_back.reset();
    scheduler.finish_packet_selection(false);
    std::vector<std::byte> received;
    bool received_fin{};
    std::size_t fragments{};
    while (!scheduler.empty())
    {
        scheduler.begin_packet_selection();
        const auto selected = scheduler.take_selected_frame(32U, 32U);
        ASSERT_TRUE(selected.has_value());
        const auto bytes = pending_test_payload(selected->frame);
        const auto decoded = cnetmod::quic::decode_frame(bytes);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_TRUE(std::holds_alternative<cnetmod::quic::stream_frame>(decoded->first));
        const auto frame = std::get<cnetmod::quic::stream_frame>(decoded->first);
        ASSERT_EQ(frame.stream_id, 4U);
        ASSERT_EQ(frame.offset, 64U + received.size());
        ASSERT_FALSE(received_fin);
        received.insert(received.end(), frame.data.begin(), frame.data.end());
        received_fin = frame.fin;
        scheduler.finish_packet_selection(true);
        ASSERT_TRUE(++fragments < 64U);
    }
    std::vector<std::byte> expected(9U, std::byte{0x70});
    expected.insert(expected.end(), 130U, std::byte{0x62});
    ASSERT_TRUE(received == expected);
    ASSERT_TRUE(received_fin);
    ASSERT_FALSE(scheduler.has_pending_writes());
    ASSERT_TRUE(owner.expired());
}
