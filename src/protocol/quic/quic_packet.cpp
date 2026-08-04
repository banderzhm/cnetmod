module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :packet;

namespace cnetmod::quic {

// =============================================================================
// Internal helpers
// =============================================================================

namespace {

    // Long header packet type bits (bits 4-5 of first byte)
    constexpr auto long_header_type(std::uint8_t first_byte) -> std::uint8_t
    {
        return (first_byte >> 4) & 0x03;
    }

    auto long_type_to_packet_type(std::uint8_t type_bits, std::uint32_t version)
        -> std::expected<packet_type, std::error_code>
    {
        if (version == quic_version_v2)
        {
            switch (type_bits)
            {
            case 0x00:
                return packet_type::retry;
            case 0x01:
                return packet_type::initial;
            case 0x02:
                return packet_type::zero_rtt;
            case 0x03:
                return packet_type::handshake;
            default:
                break;
            }
        }
        switch (type_bits)
        {
        case 0x00:
            return packet_type::initial;
        case 0x01:
            return packet_type::zero_rtt;
        case 0x02:
            return packet_type::handshake;
        case 0x03:
            return packet_type::retry;
        default:
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        }
    }

    auto read_version(std::span<const std::byte> data) -> std::uint32_t
    {
        return (std::to_integer<std::uint32_t>(data[1]) << 24) |
            (std::to_integer<std::uint32_t>(data[2]) << 16) |
            (std::to_integer<std::uint32_t>(data[3]) << 8) |
            std::to_integer<std::uint32_t>(data[4]);
    }

} // anonymous namespace

// =============================================================================
// decode_packet_type
// =============================================================================

auto decode_packet_type(std::span<const std::byte> data)
    -> std::expected<packet_type, std::error_code>
{
    if (data.empty())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    const auto first = std::to_integer<std::uint8_t>(data[0]);

    // Header form: bit 7
    const bool is_long = (first & 0x80) != 0;

    if (!is_long)
        return packet_type::one_rtt;

    if (data.size() < 5)
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    const auto version = read_version(data);
    if (version == 0)
        return packet_type::version_negotiation;

    // Check for Retry packet: type bits = 0x03
    const auto type_bits = long_header_type(first);
    return long_type_to_packet_type(type_bits, version);
}

// =============================================================================
// decode_long_header
// =============================================================================

auto decode_long_header(std::span<const std::byte> data)
    -> std::expected<long_header, std::error_code>
{
    // Minimum long header: 1 (first byte) + 4 (version) + 1 (dcid len) +
    //                      1 (scid len) = 7 bytes
    if (data.size() < 7)
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    long_header hdr{};
    std::size_t offset = 0;

    const auto first_byte = std::to_integer<std::uint8_t>(data[offset]);
    ++offset;

    if ((first_byte & 0x80) == 0)
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    if ((first_byte & 0x40) == 0)
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    // Version (4 bytes)
    hdr.version = read_version(data);
    offset += 4;

    // Version Negotiation has no QUIC long-header packet type bits, packet
    // number or length field (RFC 9000 §17.2.1).
    if (hdr.version == 0)
        hdr.type = packet_type::version_negotiation;
    else
    {
        const auto type_bits = long_header_type(first_byte);
        auto ptype = long_type_to_packet_type(type_bits, hdr.version);
        if (!ptype)
            return std::unexpected(ptype.error());
        hdr.type = *ptype;
    }

    // DCID
    const auto dcid_len = std::to_integer<std::uint8_t>(data[offset]);
    ++offset;
    if (dcid_len > max_cid_length || offset + dcid_len > data.size())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    hdr.dcid = connection_id{data.data() + offset, dcid_len};
    offset += dcid_len;

    // SCID
    const auto scid_len = std::to_integer<std::uint8_t>(data[offset]);
    ++offset;
    if (scid_len > max_cid_length || offset + scid_len > data.size())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    hdr.scid = connection_id{data.data() + offset, scid_len};
    offset += scid_len;

    if (hdr.type == packet_type::version_negotiation)
    {
        hdr.payload = data.subspan(offset);
        return hdr;
    }

    // Retry has an opaque token followed by a 16-byte Retry Integrity Tag;
    // unlike Initial/Handshake it carries neither payload length nor packet
    // number.  Integrity validation belongs to the connection state machine.
    if (hdr.type == packet_type::retry)
    {
        if (data.size() - offset < 16)
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        hdr.token = data.subspan(offset, data.size() - offset - 16);
        hdr.payload = data.subspan(data.size() - 16);
        return hdr;
    }

    // Token (Initial packets only)
    if (hdr.type == packet_type::initial)
    {
        auto token_result = decode_varint(data.subspan(offset));
        if (!token_result)
            return std::unexpected(token_result.error());
        const auto token_len = token_result->first;
        offset += token_result->second;

        if (offset + token_len > data.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        hdr.token = data.subspan(offset, token_len);
        offset += token_len;
    }

    // Payload length (varint)
    auto len_result = decode_varint(data.subspan(offset));
    if (!len_result)
        return std::unexpected(len_result.error());
    hdr.payload_length = len_result->first;
    offset += len_result->second;

    // Packet-number length is encoded in the low two bits after header
    // protection has been removed.  This decoder is also used by tests and
    // tooling on unprotected headers, so it must not hard-code four bytes.
    const auto packet_number_length = static_cast<std::size_t>((first_byte & 0x03) + 1);
    if (offset + packet_number_length > data.size())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    for (std::size_t index = 0; index < packet_number_length; ++index)
        hdr.packet_number = (hdr.packet_number << 8) |
            std::to_integer<std::uint32_t>(data[offset + index]);
    offset += packet_number_length;

    // Payload
    const auto payload_size =
        (hdr.payload_length >= packet_number_length)
        ? static_cast<std::size_t>(hdr.payload_length - packet_number_length)
        : std::size_t{0};
    if (hdr.payload_length < packet_number_length)
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    if (offset + payload_size > data.size())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    hdr.payload = data.subspan(offset, payload_size);

    return hdr;
}

// =============================================================================
// decode_short_header
// =============================================================================

auto decode_short_header(std::span<const std::byte> data,
    std::size_t dcid_length)
    -> std::expected<short_header, std::error_code>
{
    if (data.size() < 1 + dcid_length + 1)
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    short_header hdr{};
    std::size_t offset = 0;

    const auto first_byte = std::to_integer<std::uint8_t>(data[offset]);
    ++offset;

    // Must be short header (bit 7 == 0)
    if ((first_byte & 0x80) != 0)
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    // Fixed bit must be 1 (bit 6)
    if ((first_byte & 0x40) == 0)
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    hdr.spin_bit = (first_byte & 0x20) != 0;
    hdr.key_phase = (first_byte & 0x04) != 0;

    // DCID
    if (offset + dcid_length > data.size())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    hdr.dcid = connection_id{data.data() + offset,
        static_cast<std::uint8_t>(dcid_length)};
    offset += dcid_length;

    const auto packet_number_length = static_cast<std::size_t>((first_byte & 0x03) + 1);
    if (offset + packet_number_length > data.size())
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    for (std::size_t index = 0; index < packet_number_length; ++index)
        hdr.packet_number = (hdr.packet_number << 8) |
            std::to_integer<std::uint32_t>(data[offset + index]);
    offset += packet_number_length;

    // Payload
    if (offset > data.size())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    hdr.payload = data.subspan(offset);

    return hdr;
}

// =============================================================================
// Packet Number Recovery (RFC 9000 Appendix A)
// =============================================================================

auto packet_number_decode(
    std::uint32_t truncated_pn,
    std::uint32_t pn_nbits,
    std::uint64_t largest_pn)
    -> std::uint64_t
{
    if (pn_nbits == 0 || pn_nbits > 32)
        return 0;
    const std::uint64_t expected_pn = largest_pn == (std::numeric_limits<std::uint64_t>::max)()
        ? largest_pn
        : largest_pn + 1;
    const std::uint64_t pn_win = std::uint64_t{1} << pn_nbits;
    const std::uint64_t pn_hwin = pn_win / 2;
    const std::uint64_t pn_mask = pn_win - 1;

    // The incoming packet number should be greater than expected_pn - pn_hwin
    // and less than or equal to expected_pn + pn_hwin.
    const std::uint64_t candidate_pn =
        (expected_pn & ~pn_mask) | truncated_pn;

    if (candidate_pn + pn_hwin <= expected_pn)
        return candidate_pn + pn_win;
    if (candidate_pn > expected_pn + pn_hwin && candidate_pn > pn_win)
        return candidate_pn - pn_win;
    return candidate_pn;
}

auto packet_number_encode(
    std::uint64_t full_pn,
    std::uint64_t largest_acked_pn)
    -> std::pair<std::uint32_t, std::uint8_t>
{
    if (full_pn < largest_acked_pn)
        return {static_cast<std::uint32_t>(full_pn), 32};
    // Number of bits needed to represent the range
    const std::uint64_t num_unacked = full_pn - largest_acked_pn;

    // Minimum bits: need at least 2 * num_unacked to avoid ambiguity
    std::uint8_t nbits = 1;
    while ((std::uint64_t{1} << nbits) <= 2 * num_unacked)
    {
        ++nbits;
    }

    // Minimum 4 bits for encoding
    nbits = std::max<std::uint8_t>(nbits, 4);
    // Cap at 32 bits
    nbits = std::min<std::uint8_t>(nbits, 32);

    const auto mask = (std::uint64_t{1} << nbits) - 1;
    const auto truncated = static_cast<std::uint32_t>(full_pn & mask);

    return {truncated, nbits};
}

// =============================================================================
// split_coalesced_packets
// =============================================================================

auto split_coalesced_packets(std::span<const std::byte> datagram)
    -> std::expected<coalesced_packet, std::error_code>
{
    if (datagram.empty())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    coalesced_packet result{};
    std::size_t offset = 0;

    while (offset < datagram.size())
    {
        const auto remaining = datagram.subspan(offset);
        if (remaining.empty())
            break;

        const auto first = std::to_integer<std::uint8_t>(remaining[0]);
        const bool is_long = (first & 0x80) != 0;

        if (!is_long)
        {
            // A long-header packet's Length field can leave datagram padding
            // outside the protected packet.  Widely deployed implementations
            // (including aioquic and QUICHE) use this form to satisfy the
            // 1200-byte Initial datagram requirement.  Such trailing bytes do
            // not have QUIC's fixed bit and therefore are not another packet.
            // Preserve strict validation when the datagram itself starts with
            // an invalid short header.
            if ((first & 0x40U) == 0U)
            {
                if (!result.packets.empty())
                    break;
                return std::unexpected(
                    std::make_error_code(std::errc::bad_message));
            }
            // Short header: rest of datagram is this packet
            result.packets.push_back(remaining);
            break;
        }

        // Long header: need to parse length to find packet boundary
        // Minimum: 1 + 4(version) + 1(dcid_len) + 1(scid_len) = 7
        if (remaining.size() < 7)
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));

        std::size_t pos = 1; // skip first byte
        const auto version = read_version(remaining);
        pos += 4; // skip version

        const auto dcid_len = std::to_integer<std::uint8_t>(remaining[pos]);
        ++pos;
        if (dcid_len > max_cid_length || dcid_len > remaining.size() - pos)
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        pos += dcid_len;

        if (pos >= remaining.size())
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        const auto scid_len = std::to_integer<std::uint8_t>(remaining[pos]);
        ++pos;
        if (scid_len > max_cid_length || scid_len > remaining.size() - pos)
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        pos += scid_len;

        if (pos >= remaining.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));

        // Version Negotiation and Retry do not carry a Length field. They
        // consume the remainder of their UDP datagram (RFC 9000 §17.2).
        if (version == 0)
        {
            result.packets.push_back(remaining);
            break;
        }

        const auto type = long_type_to_packet_type((first >> 4) & 0x03, version);
        if (!type)
            return std::unexpected(type.error());
        if (*type == packet_type::retry)
        {
            result.packets.push_back(remaining);
            break;
        }
        if (*type == packet_type::initial)
        {
            auto token_result = decode_varint(remaining.subspan(pos));
            if (!token_result)
                return std::unexpected(token_result.error());
            const auto token_len = token_result->first;
            pos += token_result->second;
            if (token_len > remaining.size() - pos)
                return std::unexpected(std::make_error_code(std::errc::bad_message));
            pos += static_cast<std::size_t>(token_len);

            if (pos >= remaining.size())
                return std::unexpected(
                    std::make_error_code(std::errc::bad_message));
        }

        // Payload length (varint) — includes packet number + payload
        auto len_result = decode_varint(remaining.subspan(pos));
        if (!len_result)
            return std::unexpected(len_result.error());
        const auto payload_length = len_result->first;
        pos += len_result->second;

        // Total packet size = header bytes + payload_length
        const auto packet_size = pos + static_cast<std::size_t>(payload_length);
        if (packet_size > remaining.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));

        result.packets.push_back(remaining.subspan(0, packet_size));
        offset += packet_size;
    }

    if (result.packets.empty())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    return result;
}

// =============================================================================
// Packet Encoding
// =============================================================================

namespace {

    auto append_u32_be(std::vector<std::byte>& out, std::uint32_t value) -> void
    {
        out.push_back(static_cast<std::byte>((value >> 24) & 0xff));
        out.push_back(static_cast<std::byte>((value >> 16) & 0xff));
        out.push_back(static_cast<std::byte>((value >> 8) & 0xff));
        out.push_back(static_cast<std::byte>(value & 0xff));
    }

    auto append_varint_vec(std::vector<std::byte>& out, std::uint64_t value)
        -> void
    {
        const auto len = varint_size(value);
        const auto old_size = out.size();
        out.resize(old_size + len);
        std::span<std::byte> dst{out.data() + old_size, len};
        std::ignore = encode_varint_to(value, dst);
    }

} // anonymous namespace

auto encode_long_header(const long_header& hdr,
    std::span<const std::byte> payload)
    -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(64 + payload.size());

    // First byte: 1 | 1 | type(2) | reserved(2) | pn_length(2)
    std::uint8_t type_bits = 0;
    switch (hdr.type)
    {
    case packet_type::initial:
        type_bits = 0x00;
        break;
    case packet_type::zero_rtt:
        type_bits = 0x01;
        break;
    case packet_type::handshake:
        type_bits = 0x02;
        break;
    case packet_type::retry:
        type_bits = 0x03;
        break;
    default:
        type_bits = 0x00;
        break;
    }

    const std::uint8_t first_byte =
        0xC0 | (type_bits << 4) | 0x03; // pn_length = 4 (encoded as 3)
    out.push_back(static_cast<std::byte>(first_byte));

    // Version
    append_u32_be(out, hdr.version);

    // DCID
    out.push_back(static_cast<std::byte>(hdr.dcid.size()));
    if (hdr.dcid.size() > 0)
    {
        out.insert(out.end(), hdr.dcid.data(),
            hdr.dcid.data() + hdr.dcid.size());
    }

    // SCID
    out.push_back(static_cast<std::byte>(hdr.scid.size()));
    if (hdr.scid.size() > 0)
    {
        out.insert(out.end(), hdr.scid.data(),
            hdr.scid.data() + hdr.scid.size());
    }

    // Token (Initial only)
    if (hdr.type == packet_type::initial)
    {
        append_varint_vec(out, hdr.token.size());
        if (!hdr.token.empty())
        {
            out.insert(out.end(), hdr.token.begin(), hdr.token.end());
        }
    }

    // Payload length = packet_number(4 bytes) + payload
    const auto total_payload_len = 4 + payload.size();
    append_varint_vec(out, total_payload_len);

    // Packet number (4 bytes)
    append_u32_be(out, hdr.packet_number);

    // Payload
    out.insert(out.end(), payload.begin(), payload.end());

    return out;
}

auto encode_short_header(const short_header& hdr,
    std::span<const std::byte> payload)
    -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(1 + hdr.dcid.size() + 4 + payload.size());

    // First byte: 0 | 1 | spin(1) | reserved(2) | key_phase(1) | pn_length(2)
    std::uint8_t first_byte = 0x40 | 0x03; // pn_length = 4 (encoded as 3)
    if (hdr.spin_bit)
        first_byte |= 0x20;
    if (hdr.key_phase)
        first_byte |= 0x04;

    out.push_back(static_cast<std::byte>(first_byte));

    // DCID
    if (hdr.dcid.size() > 0)
    {
        out.insert(out.end(), hdr.dcid.data(),
            hdr.dcid.data() + hdr.dcid.size());
    }

    // Packet number (4 bytes)
    append_u32_be(out, hdr.packet_number);

    // Payload
    out.insert(out.end(), payload.begin(), payload.end());

    return out;
}

} // namespace cnetmod::quic
