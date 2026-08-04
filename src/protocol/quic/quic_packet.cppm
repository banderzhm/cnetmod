module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:packet;

import std;
import :types;
import :varint;

namespace cnetmod::quic {

// =============================================================================
// Packet Type
// =============================================================================

export enum class packet_type
{
    initial,
    zero_rtt,
    handshake,
    retry,
    one_rtt,
    version_negotiation,
};

// =============================================================================
// Long Header (RFC 9000 §17.2)
// =============================================================================

export struct long_header
{
    packet_type type{};
    std::uint32_t version{};
    connection_id dcid{};
    connection_id scid{};
    std::span<const std::byte> token{};
    std::uint64_t payload_length{};
    std::uint32_t packet_number{};
    std::span<const std::byte> payload{};
};

// =============================================================================
// Short Header (RFC 9000 §17.3)
// =============================================================================

export struct short_header
{
    connection_id dcid{};
    bool spin_bit{};
    bool key_phase{};
    std::uint32_t packet_number{};
    std::span<const std::byte> payload{};
};

// =============================================================================
// Coalesced Packets
// =============================================================================

export struct coalesced_packet
{
    std::vector<std::span<const std::byte>> packets{};
};

// =============================================================================
// Packet Decoding
// =============================================================================

/// Determine packet type from first byte(s).
export [[nodiscard]] auto decode_packet_type(
    std::span<const std::byte> data)
    -> std::expected<packet_type, std::error_code>;

/// Decode a long header packet.
/// Note: packet_number field is encrypted in wire format; this parses the
/// unencrypted form (after header protection removal).
export [[nodiscard]] auto decode_long_header(
    std::span<const std::byte> data)
    -> std::expected<long_header, std::error_code>;

/// Decode a short header packet.
/// Requires dcid_length to parse correctly.
export [[nodiscard]] auto decode_short_header(
    std::span<const std::byte> data, std::size_t dcid_length)
    -> std::expected<short_header, std::error_code>;

// =============================================================================
// Packet Number Recovery (RFC 9000 Appendix A)
// =============================================================================

/// Decode a truncated packet number to full packet number.
export auto packet_number_decode(
    std::uint32_t truncated_pn,
    std::uint32_t pn_nbits,
    std::uint64_t largest_pn)
    -> std::uint64_t;

/// Encode a full packet number, returning truncated PN and bits used.
export auto packet_number_encode(
    std::uint64_t full_pn,
    std::uint64_t largest_acked_pn)
    -> std::pair<std::uint32_t, std::uint8_t>;

// =============================================================================
// Coalesced Packet Splitting
// =============================================================================

/// Split a UDP datagram into individual QUIC packets.
export [[nodiscard]] auto split_coalesced_packets(
    std::span<const std::byte> datagram)
    -> std::expected<coalesced_packet, std::error_code>;

// =============================================================================
// Packet Encoding
// =============================================================================

/// Encode a long header packet.
export auto encode_long_header(
    const long_header& hdr,
    std::span<const std::byte> payload)
    -> std::vector<std::byte>;

/// Encode a short header packet.
export auto encode_short_header(
    const short_header& hdr,
    std::span<const std::byte> payload)
    -> std::vector<std::byte>;

} // namespace cnetmod::quic
