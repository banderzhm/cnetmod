#include "test_framework.hpp"

#include <array>
#include <sstream>
#include <vector>

import cnetmod.protocol.quic;
import std;

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

RUN_TESTS();
