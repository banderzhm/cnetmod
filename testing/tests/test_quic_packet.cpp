#include "test_framework.hpp"

#include <array>
#include <sstream>
#include <vector>

import cnetmod.protocol.quic;

// =============================================================================
// Helper: Construct a valid Initial long header packet
// =============================================================================
auto make_initial_packet() -> std::vector<std::byte>
{
    return {
        // First byte: 0xC3 = Long(1) | Fixed(1) | Initial(00) | Reserved(00) | PN_len(11)
        static_cast<std::byte>(0xC3),
        // Version: 0x00000001
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x01),
        // DCID len: 4
        static_cast<std::byte>(0x04),
        // DCID: 01 02 03 04
        static_cast<std::byte>(0x01),
        static_cast<std::byte>(0x02),
        static_cast<std::byte>(0x03),
        static_cast<std::byte>(0x04),
        // SCID len: 4
        static_cast<std::byte>(0x04),
        // SCID: 05 06 07 08
        static_cast<std::byte>(0x05),
        static_cast<std::byte>(0x06),
        static_cast<std::byte>(0x07),
        static_cast<std::byte>(0x08),
        // Token length: 0 (varint)
        static_cast<std::byte>(0x00),
        // Payload length: 8 (varint) = 4 bytes PN + 4 bytes payload
        static_cast<std::byte>(0x08),
        // Packet number (4 bytes)
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x01),
        // Payload (4 bytes)
        static_cast<std::byte>(0xAA),
        static_cast<std::byte>(0xBB),
        static_cast<std::byte>(0xCC),
        static_cast<std::byte>(0xDD),
    };
}

// =============================================================================
// Helper: Construct a valid Handshake long header packet
// =============================================================================
auto make_handshake_packet() -> std::vector<std::byte>
{
    return {
        // First byte: 0xE3 = Long(1) | Fixed(1) | Handshake(10) | Reserved(00) | PN_len(11)
        static_cast<std::byte>(0xE3),
        // Version: 0x00000001
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x01),
        // DCID len: 4
        static_cast<std::byte>(0x04),
        // DCID: 01 02 03 04
        static_cast<std::byte>(0x01),
        static_cast<std::byte>(0x02),
        static_cast<std::byte>(0x03),
        static_cast<std::byte>(0x04),
        // SCID len: 4
        static_cast<std::byte>(0x04),
        // SCID: 05 06 07 08
        static_cast<std::byte>(0x05),
        static_cast<std::byte>(0x06),
        static_cast<std::byte>(0x07),
        static_cast<std::byte>(0x08),
        // (No token for Handshake)
        // Payload length: 8 (varint) = 4 bytes PN + 4 bytes payload
        static_cast<std::byte>(0x08),
        // Packet number (4 bytes)
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x02),
        // Payload (4 bytes)
        static_cast<std::byte>(0xAA),
        static_cast<std::byte>(0xBB),
        static_cast<std::byte>(0xCC),
        static_cast<std::byte>(0xDD),
    };
}

// =============================================================================
// Helper: Construct a valid Short header (1-RTT) packet
// =============================================================================
auto make_short_packet() -> std::vector<std::byte>
{
    return {
        // First byte: 0x43 = Short(0) | Fixed(1) | Spin(0) | Reserved(00) | KP(0) | PN_len(11)
        static_cast<std::byte>(0x43),
        // DCID (4 bytes): 01 02 03 04
        static_cast<std::byte>(0x01),
        static_cast<std::byte>(0x02),
        static_cast<std::byte>(0x03),
        static_cast<std::byte>(0x04),
        // Packet number (4 bytes)
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x05),
        // Payload
        static_cast<std::byte>(0xEE),
        static_cast<std::byte>(0xFF),
    };
}

// =============================================================================
// Tests: Packet type detection
// =============================================================================

TEST(long_header_initial_type_detection)
{
    auto packet = make_initial_packet();
    auto result = cnetmod::quic::decode_packet_type(
        std::span{packet.data(), packet.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(*result == cnetmod::quic::packet_type::initial);
}

TEST(long_header_handshake_type_detection)
{
    auto packet = make_handshake_packet();
    auto result = cnetmod::quic::decode_packet_type(
        std::span{packet.data(), packet.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(*result == cnetmod::quic::packet_type::handshake);
}

TEST(short_header_1rtt_type_detection)
{
    auto packet = make_short_packet();
    auto result = cnetmod::quic::decode_packet_type(
        std::span{packet.data(), packet.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(*result == cnetmod::quic::packet_type::one_rtt);
}

TEST(zero_rtt_type_detection)
{
    // 0-RTT: type bits = 01, first byte = 0xD0 | pn_len
    std::vector<std::byte> zero_rtt = {
        static_cast<std::byte>(0xD3), // Long(1) | Fixed(1) | 0-RTT(01) | PN(11)
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x01),
        static_cast<std::byte>(0x00), // DCID len = 0
        static_cast<std::byte>(0x00), // SCID len = 0
    };

    auto result = cnetmod::quic::decode_packet_type(
        std::span{zero_rtt.data(), zero_rtt.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(*result == cnetmod::quic::packet_type::zero_rtt);
}

TEST(retry_type_detection)
{
    // Retry: type bits = 11, first byte = 0xF0 | pn_len
    std::vector<std::byte> retry = {
        static_cast<std::byte>(0xF3), // Long(1) | Fixed(1) | Retry(11) | PN(11)
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x01),
        static_cast<std::byte>(0x00), // DCID len = 0
        static_cast<std::byte>(0x00), // SCID len = 0
    };

    auto result = cnetmod::quic::decode_packet_type(
        std::span{retry.data(), retry.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(*result == cnetmod::quic::packet_type::retry);
}

TEST(version_negotiation_detection)
{
    // Version = 0x00000000 triggers version negotiation
    std::vector<std::byte> vn = {
        static_cast<std::byte>(0x80), // Long header
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00), // DCID len = 0
        static_cast<std::byte>(0x00), // SCID len = 0
    };

    auto result = cnetmod::quic::decode_packet_type(
        std::span{vn.data(), vn.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(*result == cnetmod::quic::packet_type::version_negotiation);
}

// =============================================================================
// Tests: Long header parsing
// =============================================================================

TEST(long_header_initial_full_parse)
{
    auto packet = make_initial_packet();

    auto result = cnetmod::quic::decode_long_header(
        std::span{packet.data(), packet.size()});
    ASSERT_TRUE(result.has_value());

    auto& hdr = result.value();
    ASSERT_TRUE(hdr.type == cnetmod::quic::packet_type::initial);
    ASSERT_EQ(hdr.version, 0x00000001U);
    ASSERT_EQ(hdr.dcid.size(), 4);
    ASSERT_EQ(hdr.scid.size(), 4);
    ASSERT_EQ(hdr.packet_number, 1U);
    ASSERT_EQ(hdr.payload_length, 8ULL);
}

TEST(long_header_handshake_full_parse)
{
    auto packet = make_handshake_packet();

    auto result = cnetmod::quic::decode_long_header(
        std::span{packet.data(), packet.size()});
    ASSERT_TRUE(result.has_value());

    auto& hdr = result.value();
    ASSERT_TRUE(hdr.type == cnetmod::quic::packet_type::handshake);
    ASSERT_EQ(hdr.version, 0x00000001U);
    ASSERT_EQ(hdr.dcid.size(), 4);
    ASSERT_EQ(hdr.scid.size(), 4);
    ASSERT_EQ(hdr.packet_number, 2U);
}

TEST(long_header_initial_with_token)
{
    // Initial packet with a non-empty token
    std::vector<std::byte> packet = {
        static_cast<std::byte>(0xC3),
        // Version
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x01),
        // DCID len = 2
        static_cast<std::byte>(0x02),
        static_cast<std::byte>(0xAB),
        static_cast<std::byte>(0xCD),
        // SCID len = 0
        static_cast<std::byte>(0x00),
        // Token length: 4 (varint)
        static_cast<std::byte>(0x04),
        // Token data (4 bytes)
        static_cast<std::byte>(0x11),
        static_cast<std::byte>(0x22),
        static_cast<std::byte>(0x33),
        static_cast<std::byte>(0x44),
        // Payload length: 6 (varint) = 4 PN + 2 payload
        static_cast<std::byte>(0x06),
        // Packet number
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x0A),
        // Payload (2 bytes)
        static_cast<std::byte>(0xEE),
        static_cast<std::byte>(0xFF),
    };

    auto result = cnetmod::quic::decode_long_header(
        std::span{packet.data(), packet.size()});
    ASSERT_TRUE(result.has_value());

    auto& hdr = result.value();
    ASSERT_TRUE(hdr.type == cnetmod::quic::packet_type::initial);
    ASSERT_EQ(hdr.token.size(), 4u);
    ASSERT_EQ(hdr.packet_number, 10U);
}

TEST(long_header_decode_too_short)
{
    // Minimum long header is 7 bytes; anything less should fail
    std::vector<std::byte> truncated(6, static_cast<std::byte>(0xC0));
    auto result = cnetmod::quic::decode_long_header(
        std::span{truncated.data(), truncated.size()});
    ASSERT_FALSE(result.has_value());
}

TEST(long_header_short_header_rejected)
{
    // Short header passed to decode_long_header should fail
    auto packet = make_short_packet();
    auto result = cnetmod::quic::decode_long_header(
        std::span{packet.data(), packet.size()});
    ASSERT_FALSE(result.has_value());
}

// =============================================================================
// Tests: Short header parsing
// =============================================================================

TEST(short_header_1rtt_full_parse)
{
    auto packet = make_short_packet();

    auto result = cnetmod::quic::decode_short_header(
        std::span{packet.data(), packet.size()}, 4);
    ASSERT_TRUE(result.has_value());

    auto& hdr = result.value();
    ASSERT_EQ(hdr.dcid.size(), 4);
    ASSERT_EQ(hdr.packet_number, 5U);
    ASSERT_FALSE(hdr.spin_bit);
    ASSERT_FALSE(hdr.key_phase);
}

TEST(short_header_key_phase_bit)
{
    // Short header with key phase = 1
    std::vector<std::byte> packet = {
        static_cast<std::byte>(0x47), // 0100_0111 = Short|Fixed|KP=1|PN_len=11
        // DCID (4 bytes)
        static_cast<std::byte>(0x01),
        static_cast<std::byte>(0x02),
        static_cast<std::byte>(0x03),
        static_cast<std::byte>(0x04),
        // PN (4 bytes)
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x0F),
        // Payload
        static_cast<std::byte>(0xAA),
    };

    auto result = cnetmod::quic::decode_short_header(
        std::span{packet.data(), packet.size()}, 4);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value().key_phase);
    ASSERT_EQ(result.value().packet_number, 15U);
}

TEST(short_header_spin_bit)
{
    // Short header with spin bit = 1
    std::vector<std::byte> packet = {
        static_cast<std::byte>(0x63), // 0110_0011 = Short|Fixed|Spin=1|PN_len=11
        // DCID (2 bytes)
        static_cast<std::byte>(0xAA),
        static_cast<std::byte>(0xBB),
        // PN (4 bytes)
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x20),
        // Payload
        static_cast<std::byte>(0xCC),
    };

    auto result = cnetmod::quic::decode_short_header(
        std::span{packet.data(), packet.size()}, 2);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value().spin_bit);
}

TEST(short_header_too_short_rejected)
{
    // Short header with insufficient bytes (needs 1 + dcid_len + 4)
    std::vector<std::byte> truncated = {
        static_cast<std::byte>(0x43),
        static_cast<std::byte>(0x01),
        static_cast<std::byte>(0x02),
    };

    auto result = cnetmod::quic::decode_short_header(
        std::span{truncated.data(), truncated.size()}, 4);
    ASSERT_FALSE(result.has_value());
}

// =============================================================================
// Tests: Packet Number Recovery (RFC 9000 Appendix A)
// =============================================================================

TEST(packet_number_decode_basic_recovery)
{
    // Simple case: truncated_pn = 0x234, largest_pn = 0x1000, pn_nbits = 12
    // Expected: 0x1234
    auto recovered = cnetmod::quic::packet_number_decode(
        0x234, // truncated_pn
        12,    // pn_nbits (12-bit PN field)
        0x1000 // largest_pn
    );
    ASSERT_EQ(recovered, 0x1234ULL);
}

TEST(packet_number_decode_wraparound)
{
    // Edge case: wrap-around
    // largest_pn = 0xFFFF, truncated_pn = 0x0000 (next packet after wrap)
    // Expected: 0x10000
    auto recovered = cnetmod::quic::packet_number_decode(
        0x0000, // truncated_pn
        16,     // pn_nbits (16-bit PN field)
        0xFFFF  // largest_pn
    );
    ASSERT_EQ(recovered, 0x10000ULL);
}

TEST(packet_number_decode_first_packet)
{
    // First packet: largest_pn = 0, truncated_pn = 0
    auto recovered = cnetmod::quic::packet_number_decode(
        0, // truncated_pn
        8, // pn_nbits
        0  // largest_pn (no previous packet)
    );
    ASSERT_EQ(recovered, 0ULL);
}

TEST(packet_number_encode_decode_roundtrip)
{
    // Encode then decode should recover the original PN
    for (std::uint64_t full_pn : {0ULL, 1ULL, 100ULL, 1000ULL, 65535ULL, 100000ULL})
    {
        std::uint64_t largest_acked = (full_pn > 0) ? full_pn - 1 : 0;

        auto [truncated, nbits] = cnetmod::quic::packet_number_encode(full_pn, largest_acked);

        auto recovered = cnetmod::quic::packet_number_decode(
            truncated, nbits, largest_acked);
        ASSERT_EQ(recovered, full_pn);
    }
}

TEST(packet_number_encode_min_bits)
{
    // packet_number_encode should use minimum 4 bits
    auto [truncated, nbits] = cnetmod::quic::packet_number_encode(1, 0);
    ASSERT_GE(static_cast<int>(nbits), 4);

    // Truncated PN should be the low bits of full_pn
    ASSERT_EQ(truncated, 1U);
}

// =============================================================================
// Tests: Coalesced packet splitting
// =============================================================================

TEST(coalesced_single_initial_packet)
{
    auto packet = make_initial_packet();

    auto result = cnetmod::quic::split_coalesced_packets(
        std::span{packet.data(), packet.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().packets.size(), 1u);
}

TEST(coalesced_two_long_header_packets)
{
    // Concatenate two long header packets
    auto initial = make_initial_packet();
    auto handshake = make_handshake_packet();

    std::vector<std::byte> datagram;
    datagram.insert(datagram.end(), initial.begin(), initial.end());
    datagram.insert(datagram.end(), handshake.begin(), handshake.end());

    auto result = cnetmod::quic::split_coalesced_packets(
        std::span{datagram.data(), datagram.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().packets.size(), 2u);

    // Verify first packet is Initial
    auto type1 = cnetmod::quic::decode_packet_type(result.value().packets[0]);
    ASSERT_TRUE(type1.has_value());
    ASSERT_TRUE(*type1 == cnetmod::quic::packet_type::initial);

    // Verify second packet is Handshake
    auto type2 = cnetmod::quic::decode_packet_type(result.value().packets[1]);
    ASSERT_TRUE(type2.has_value());
    ASSERT_TRUE(*type2 == cnetmod::quic::packet_type::handshake);
}

TEST(initial_datagram_trailing_padding_is_not_a_short_packet)
{
    auto datagram = make_initial_packet();
    datagram.resize(cnetmod::quic::min_initial_pkt_size, std::byte{0});

    auto result = cnetmod::quic::split_coalesced_packets(
        std::span{datagram.data(), datagram.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->packets.size(), 1u);
    ASSERT_EQ(result->packets.front().size(), make_initial_packet().size());
}

TEST(coalesced_long_then_short)
{
    // Long header followed by short header (short consumes remaining datagram)
    auto initial = make_initial_packet();
    auto short_pkt = make_short_packet();

    std::vector<std::byte> datagram;
    datagram.insert(datagram.end(), initial.begin(), initial.end());
    datagram.insert(datagram.end(), short_pkt.begin(), short_pkt.end());

    auto result = cnetmod::quic::split_coalesced_packets(
        std::span{datagram.data(), datagram.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().packets.size(), 2u);
}

TEST(coalesced_empty_datagram_rejected)
{
    std::vector<std::byte> empty;
    auto result = cnetmod::quic::split_coalesced_packets(
        std::span{empty.data(), empty.size()});
    ASSERT_FALSE(result.has_value());
}

// =============================================================================
// Tests: Packet encoding round-trip
// =============================================================================

TEST(long_header_encode_decode_roundtrip)
{
    cnetmod::quic::long_header hdr;
    hdr.type = cnetmod::quic::packet_type::initial;
    hdr.version = 0x00000001;

    // Set DCID
    std::array<std::byte, 4> dcid_data = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    hdr.dcid = cnetmod::quic::connection_id{dcid_data.data(), 4};

    // Set SCID
    std::array<std::byte, 4> scid_data = {
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}};
    hdr.scid = cnetmod::quic::connection_id{scid_data.data(), 4};

    hdr.packet_number = 42;

    // Payload
    std::vector<std::byte> payload = {
        std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};

    auto encoded = cnetmod::quic::encode_long_header(hdr, std::span{payload.data(), payload.size()});
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    auto decoded = cnetmod::quic::decode_long_header(
        std::span{encoded.data(), encoded.size()});
    ASSERT_TRUE(decoded.has_value());

    auto& decoded_hdr = decoded.value();
    ASSERT_TRUE(decoded_hdr.type == cnetmod::quic::packet_type::initial);
    ASSERT_EQ(decoded_hdr.version, 0x00000001U);
    ASSERT_EQ(decoded_hdr.dcid.size(), 4);
    ASSERT_EQ(decoded_hdr.scid.size(), 4);
    ASSERT_EQ(decoded_hdr.packet_number, 42U);
}

TEST(short_header_encode_decode_roundtrip)
{
    cnetmod::quic::short_header hdr;

    std::array<std::byte, 4> dcid_data = {
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    hdr.dcid = cnetmod::quic::connection_id{dcid_data.data(), 4};
    hdr.spin_bit = true;
    hdr.key_phase = false;
    hdr.packet_number = 100;

    std::vector<std::byte> payload = {
        std::byte{0x11}, std::byte{0x22}};

    auto encoded = cnetmod::quic::encode_short_header(hdr, std::span{payload.data(), payload.size()});
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    auto decoded = cnetmod::quic::decode_short_header(
        std::span{encoded.data(), encoded.size()}, 4);
    ASSERT_TRUE(decoded.has_value());

    auto& decoded_hdr = decoded.value();
    ASSERT_EQ(decoded_hdr.dcid.size(), 4);
    ASSERT_EQ(decoded_hdr.packet_number, 100U);
    ASSERT_TRUE(decoded_hdr.spin_bit);
    ASSERT_FALSE(decoded_hdr.key_phase);
}

TEST(empty_input_rejected)
{
    std::vector<std::byte> empty;
    auto result = cnetmod::quic::decode_packet_type(
        std::span{empty.data(), empty.size()});
    ASSERT_FALSE(result.has_value());
}

// RFC 9000 section 17.2 limits connection IDs to 20 octets.  Rejecting an
// oversized length before slicing the datagram is part of the packet parser's
// untrusted-input boundary.
TEST(long_header_rejects_connection_id_longer_than_rfc_limit)
{
    const std::vector<std::byte> packet = {
        std::byte{0xc0},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x15}, // DCID length = 21, RFC 9000 maximum is 20.
    };

    const auto decoded = cnetmod::quic::decode_long_header(packet);
    ASSERT_FALSE(decoded.has_value());
    ASSERT_EQ(decoded.error(), std::make_error_code(std::errc::bad_message));
}

// RFC 9000 section 17.2.5 requires a Retry Integrity Tag of 16 octets.
TEST(retry_without_integrity_tag_is_rejected)
{
    const std::vector<std::byte> packet = {
        std::byte{0xf0},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x00}, // Empty DCID and SCID.
        std::byte{0xaa},
        std::byte{0xbb}, // Neither token nor tag is complete.
    };

    const auto decoded = cnetmod::quic::decode_long_header(packet);
    ASSERT_FALSE(decoded.has_value());
    ASSERT_EQ(decoded.error(), std::make_error_code(std::errc::bad_message));
}

// A coalesced parser must never trust a declared payload length past the UDP
// datagram.  This guards the length arithmetic used before packet protection.
TEST(coalesced_packet_with_truncated_declared_payload_is_rejected)
{
    auto packet = make_initial_packet();
    // Initial token length occupies byte 15.  The following byte is payload
    // length; declare 63 bytes while only eight bytes (PN + payload) remain.
    packet[16] = std::byte{0x3f};

    const auto split = cnetmod::quic::split_coalesced_packets(packet);
    ASSERT_FALSE(split.has_value());
    ASSERT_EQ(split.error(), std::make_error_code(std::errc::bad_message));
}

// RFC 9001 section 5.4.4 interprets the first four ChaCha20 sample bytes as
// a little-endian block counter. This is deliberately not a native-endian
// load so packet protection remains identical on every supported platform.
TEST(chacha20_header_protection_uses_little_endian_counter)
{
    cnetmod::quic::quic_level_keys keys;
    keys.cipher_id = 0x1303; // TLS_CHACHA20_POLY1305_SHA256
    keys.hp_key = {
        0x25,
        0xa2,
        0x82,
        0xb9,
        0xe8,
        0x2f,
        0x06,
        0xf2,
        0x1f,
        0x48,
        0x89,
        0x17,
        0xa4,
        0xfc,
        0x8f,
        0x1b,
        0x73,
        0x57,
        0x36,
        0x85,
        0x60,
        0x85,
        0x97,
        0xd0,
        0xef,
        0xcb,
        0x07,
        0x6b,
        0x0a,
        0xb7,
        0xa7,
        0xa4,
    };
    std::vector<std::byte> packet = {
        std::byte{0x43},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x5e},
        std::byte{0x5c},
        std::byte{0xd5},
        std::byte{0x5c},
        std::byte{0x41},
        std::byte{0xf6},
        std::byte{0x90},
        std::byte{0x80},
        std::byte{0x57},
        std::byte{0x5d},
        std::byte{0x79},
        std::byte{0x99},
        std::byte{0xc2},
        std::byte{0x5a},
        std::byte{0x5b},
        std::byte{0xfb},
    };

    const auto protected_header = cnetmod::quic::protect_header(
        keys, packet, 1U, false);
    ASSERT_TRUE(protected_header.has_value());
    ASSERT_EQ(std::to_integer<unsigned>(packet[0]), 0x4dU);
    ASSERT_EQ(std::to_integer<unsigned>(packet[1]), 0xfeU);
    ASSERT_EQ(std::to_integer<unsigned>(packet[2]), 0xfeU);
    ASSERT_EQ(std::to_integer<unsigned>(packet[3]), 0x7dU);
    ASSERT_EQ(std::to_integer<unsigned>(packet[4]), 0x03U);
}

RUN_TESTS();
