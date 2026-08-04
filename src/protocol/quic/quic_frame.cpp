module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :frame;

namespace cnetmod::quic {

namespace {

// Helper: append varint bytes to vector
auto append_varint(std::vector<std::byte>& out, std::uint64_t value) -> void
{
    const auto len = varint_size(value);
    const auto old_size = out.size();
    out.resize(old_size + len);
    std::span<std::byte> dst{out.data() + old_size, len};
    std::ignore = encode_varint_to(value, dst);
}

// Helper: append raw bytes to vector
auto append_bytes(std::vector<std::byte>& out, std::span<const std::byte> data)
    -> void
{
    out.insert(out.end(), data.begin(), data.end());
}

auto decode_ack_frame(std::span<const std::byte> data, bool has_ecn)
    -> std::expected<std::pair<ack_frame, std::size_t>, std::error_code>
{
    ack_frame frame{};
    frame.has_ecn = has_ecn;
    std::size_t offset = 0;

    auto read_varint = [&]() -> std::expected<std::uint64_t, std::error_code>
    {
        auto result = decode_varint(data.subspan(offset));
        if (!result)
            return std::unexpected(result.error());
        offset += result->second;
        return result->first;
    };

    auto largest = read_varint();
    if (!largest)
        return std::unexpected(largest.error());
    frame.largest_acked = *largest;

    auto delay = read_varint();
    if (!delay)
        return std::unexpected(delay.error());
    frame.ack_delay = *delay;

    auto range_count = read_varint();
    if (!range_count)
        return std::unexpected(range_count.error());
    frame.ack_range_count = *range_count;

    auto first_range = read_varint();
    if (!first_range)
        return std::unexpected(first_range.error());
    frame.first_ack_range = *first_range;

    frame.ack_ranges.reserve(static_cast<std::size_t>(frame.ack_range_count));
    for (std::uint64_t i = 0; i < frame.ack_range_count; ++i)
    {
        ack_range range{};
        auto gap = read_varint();
        if (!gap)
            return std::unexpected(gap.error());
        range.gap = *gap;

        auto range_len = read_varint();
        if (!range_len)
            return std::unexpected(range_len.error());
        range.ack_range_length = *range_len;

        frame.ack_ranges.push_back(range);
    }

    if (has_ecn)
    {
        auto ect_0 = read_varint();
        if (!ect_0)
            return std::unexpected(ect_0.error());
        frame.ect_0_count = *ect_0;

        auto ect_1 = read_varint();
        if (!ect_1)
            return std::unexpected(ect_1.error());
        frame.ect_1_count = *ect_1;

        auto ecn_ce = read_varint();
        if (!ecn_ce)
            return std::unexpected(ecn_ce.error());
        frame.ecn_ce_count = *ecn_ce;
    }

    return std::pair{frame, offset};
}

auto decode_stream_frame(std::span<const std::byte> data,
    std::uint64_t type_value)
    -> std::expected<std::pair<stream_frame, std::size_t>, std::error_code>
{
    stream_frame frame{};
    std::size_t offset = 0;

    // Low 3 bits of type: bit 0 = FIN, bit 1 = LEN present, bit 2 = OFF present
    const bool has_off = (type_value & 0x04) != 0;
    const bool has_len = (type_value & 0x02) != 0;
    frame.fin = (type_value & 0x01) != 0;

    auto read_varint = [&]() -> std::expected<std::uint64_t, std::error_code>
    {
        auto result = decode_varint(data.subspan(offset));
        if (!result)
            return std::unexpected(result.error());
        offset += result->second;
        return result->first;
    };

    auto sid = read_varint();
    if (!sid)
        return std::unexpected(sid.error());
    frame.stream_id = *sid;

    if (has_off)
    {
        auto off = read_varint();
        if (!off)
            return std::unexpected(off.error());
        frame.offset = *off;
    }

    if (has_len)
    {
        auto len = read_varint();
        if (!len)
            return std::unexpected(len.error());
        if (offset + *len > data.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        frame.data = data.subspan(offset, *len);
        offset += *len;
    }
    else
    {
        // No length field: data extends to end of packet
        frame.data = data.subspan(offset);
        offset = data.size();
    }

    return std::pair{frame, offset};
}

auto decode_connection_close_frame(std::span<const std::byte> data,
    bool is_app_error)
    -> std::expected<std::pair<connection_close_frame, std::size_t>,
        std::error_code>
{
    connection_close_frame frame{};
    frame.is_application_error = is_app_error;
    std::size_t offset = 0;

    auto read_varint = [&]() -> std::expected<std::uint64_t, std::error_code>
    {
        auto result = decode_varint(data.subspan(offset));
        if (!result)
            return std::unexpected(result.error());
        offset += result->second;
        return result->first;
    };

    auto err = read_varint();
    if (!err)
        return std::unexpected(err.error());
    frame.error_code = *err;

    if (!is_app_error)
    {
        auto ft = read_varint();
        if (!ft)
            return std::unexpected(ft.error());
        frame.frame_type_value = *ft;
    }

    auto reason_len = read_varint();
    if (!reason_len)
        return std::unexpected(reason_len.error());

    if (offset + *reason_len > data.size())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    frame.reason.reserve(*reason_len);
    for (std::uint64_t i = 0; i < *reason_len; ++i)
    {
        frame.reason += static_cast<char>(
            std::to_integer<std::uint8_t>(data[offset + i]));
    }
    offset += *reason_len;

    return std::pair{frame, offset};
}

} // anonymous namespace

auto decode_frame(std::span<const std::byte> data)
    -> std::expected<std::pair<quic_frame_variant, std::size_t>,
        std::error_code>
{
    if (data.empty())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    // Decode frame type as varint
    auto type_result = decode_varint(data);
    if (!type_result)
        return std::unexpected(type_result.error());

    const auto type_value = type_result->first;
    const auto type_offset = type_result->second;
    const auto payload = data.subspan(type_offset);

    auto read_varint_at = [&](std::size_t& off)
        -> std::expected<std::uint64_t, std::error_code>
    {
        auto result = decode_varint(payload.subspan(off));
        if (!result)
            return std::unexpected(result.error());
        off += result->second;
        return result->first;
    };

    switch (type_value)
    {
    case 0x00: // PADDING
        return std::pair{quic_frame_variant{padding_frame{}}, type_offset};

    case 0x01: // PING
        return std::pair{quic_frame_variant{ping_frame{}}, type_offset};

    case 0x02: // ACK
    case 0x03: // ACK_ECN
    {
        auto result = decode_ack_frame(payload, type_value == 0x03);
        if (!result)
            return std::unexpected(result.error());
        return std::pair{quic_frame_variant{result->first},
            type_offset + result->second};
    }

    case 0x04: // RESET_STREAM
    {
        reset_stream_frame frame{};
        std::size_t off = 0;
        auto sid = read_varint_at(off);
        if (!sid)
            return std::unexpected(sid.error());
        frame.stream_id = *sid;
        auto err = read_varint_at(off);
        if (!err)
            return std::unexpected(err.error());
        frame.application_error_code = *err;
        auto fs = read_varint_at(off);
        if (!fs)
            return std::unexpected(fs.error());
        frame.final_size = *fs;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x05: // STOP_SENDING
    {
        stop_sending_frame frame{};
        std::size_t off = 0;
        auto sid = read_varint_at(off);
        if (!sid)
            return std::unexpected(sid.error());
        frame.stream_id = *sid;
        auto err = read_varint_at(off);
        if (!err)
            return std::unexpected(err.error());
        frame.application_error_code = *err;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x06: // CRYPTO
    {
        crypto_frame frame{};
        std::size_t off = 0;
        auto offset_val = read_varint_at(off);
        if (!offset_val)
            return std::unexpected(offset_val.error());
        frame.offset = *offset_val;
        auto len = read_varint_at(off);
        if (!len)
            return std::unexpected(len.error());
        if (off + *len > payload.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        frame.data = payload.subspan(off, *len);
        off += *len;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x07: // NEW_TOKEN
    {
        new_token_frame frame{};
        std::size_t off = 0;
        auto len = read_varint_at(off);
        if (!len)
            return std::unexpected(len.error());
        if (off + *len > payload.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        frame.token = payload.subspan(off, *len);
        off += *len;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    default:
        break;
    }

    // STREAM frames: 0x08-0x0F
    if (type_value >= 0x08 && type_value <= 0x0F)
    {
        auto result = decode_stream_frame(payload, type_value);
        if (!result)
            return std::unexpected(result.error());
        return std::pair{quic_frame_variant{result->first},
            type_offset + result->second};
    }

    switch (type_value)
    {
    case 0x10: // MAX_DATA
    {
        max_data_frame frame{};
        std::size_t off = 0;
        auto val = read_varint_at(off);
        if (!val)
            return std::unexpected(val.error());
        frame.maximum = *val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x11: // MAX_STREAM_DATA
    {
        max_stream_data_frame frame{};
        std::size_t off = 0;
        auto sid = read_varint_at(off);
        if (!sid)
            return std::unexpected(sid.error());
        frame.stream_id = *sid;
        auto max_val = read_varint_at(off);
        if (!max_val)
            return std::unexpected(max_val.error());
        frame.maximum = *max_val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x12: // MAX_STREAMS (bidirectional)
    {
        max_streams_frame frame{};
        frame.bidirectional = true;
        std::size_t off = 0;
        auto val = read_varint_at(off);
        if (!val)
            return std::unexpected(val.error());
        frame.maximum = *val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x13: // MAX_STREAMS (unidirectional)
    {
        max_streams_frame frame{};
        frame.bidirectional = false;
        std::size_t off = 0;
        auto val = read_varint_at(off);
        if (!val)
            return std::unexpected(val.error());
        frame.maximum = *val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x14: // DATA_BLOCKED
    {
        data_blocked_frame frame{};
        std::size_t off = 0;
        auto val = read_varint_at(off);
        if (!val)
            return std::unexpected(val.error());
        frame.maximum_data = *val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x15: // STREAM_DATA_BLOCKED
    {
        stream_data_blocked_frame frame{};
        std::size_t off = 0;
        auto sid = read_varint_at(off);
        if (!sid)
            return std::unexpected(sid.error());
        frame.stream_id = *sid;
        auto max_val = read_varint_at(off);
        if (!max_val)
            return std::unexpected(max_val.error());
        frame.maximum_stream_data = *max_val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x16: // STREAMS_BLOCKED (bidirectional)
    {
        streams_blocked_frame frame{};
        frame.bidirectional = true;
        std::size_t off = 0;
        auto val = read_varint_at(off);
        if (!val)
            return std::unexpected(val.error());
        frame.maximum = *val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x17: // STREAMS_BLOCKED (unidirectional)
    {
        streams_blocked_frame frame{};
        frame.bidirectional = false;
        std::size_t off = 0;
        auto val = read_varint_at(off);
        if (!val)
            return std::unexpected(val.error());
        frame.maximum = *val;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x18: // NEW_CONNECTION_ID
    {
        new_connection_id_frame frame{};
        std::size_t off = 0;
        auto seq = read_varint_at(off);
        if (!seq)
            return std::unexpected(seq.error());
        frame.sequence_number = *seq;
        auto retire = read_varint_at(off);
        if (!retire)
            return std::unexpected(retire.error());
        frame.retire_prior_to = *retire;

        if (off >= payload.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        const auto cid_len = std::to_integer<std::uint8_t>(payload[off]);
        ++off;
        if (cid_len > max_cid_length || off + cid_len + 16 > payload.size())
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        frame.cid = connection_id{payload.data() + off, cid_len};
        off += cid_len;
        std::copy_n(payload.data() + off, 16,
            frame.stateless_reset_token.begin());
        off += 16;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x19: // RETIRE_CONNECTION_ID
    {
        retire_connection_id_frame frame{};
        std::size_t off = 0;
        auto seq = read_varint_at(off);
        if (!seq)
            return std::unexpected(seq.error());
        frame.sequence_number = *seq;
        return std::pair{quic_frame_variant{frame}, type_offset + off};
    }

    case 0x1A: // PATH_CHALLENGE
    {
        if (payload.size() < 8)
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        path_challenge_frame frame{};
        std::copy_n(payload.data(), 8, frame.data.begin());
        return std::pair{quic_frame_variant{frame}, type_offset + 8};
    }

    case 0x1B: // PATH_RESPONSE
    {
        if (payload.size() < 8)
            return std::unexpected(
                std::make_error_code(std::errc::bad_message));
        path_response_frame frame{};
        std::copy_n(payload.data(), 8, frame.data.begin());
        return std::pair{quic_frame_variant{frame}, type_offset + 8};
    }

    case 0x1C: // CONNECTION_CLOSE (transport)
    case 0x1D: // CONNECTION_CLOSE (application)
    {
        auto result = decode_connection_close_frame(
            payload, type_value == 0x1D);
        if (!result)
            return std::unexpected(result.error());
        return std::pair{quic_frame_variant{result->first},
            type_offset + result->second};
    }

    case 0x1E: // HANDSHAKE_DONE
        return std::pair{
            quic_frame_variant{handshake_done_frame{}}, type_offset};

    default:
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    }
}

// =============================================================================
// Frame Encoders
// =============================================================================

auto encode_frame(const padding_frame& /*f*/) -> std::vector<std::byte>
{
    return {static_cast<std::byte>(0x00)};
}

auto encode_frame(const ping_frame& /*f*/) -> std::vector<std::byte>
{
    return {static_cast<std::byte>(0x01)};
}

auto encode_frame(const ack_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(64);
    append_varint(out, f.has_ecn ? 0x03 : 0x02);
    append_varint(out, f.largest_acked);
    append_varint(out, f.ack_delay);
    append_varint(out, f.ack_range_count);
    append_varint(out, f.first_ack_range);
    for (const auto& range : f.ack_ranges)
    {
        append_varint(out, range.gap);
        append_varint(out, range.ack_range_length);
    }
    if (f.has_ecn)
    {
        append_varint(out, f.ect_0_count);
        append_varint(out, f.ect_1_count);
        append_varint(out, f.ecn_ce_count);
    }
    return out;
}

auto encode_frame(const reset_stream_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(24);
    append_varint(out, 0x04);
    append_varint(out, f.stream_id);
    append_varint(out, f.application_error_code);
    append_varint(out, f.final_size);
    return out;
}

auto encode_frame(const stop_sending_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(16);
    append_varint(out, 0x05);
    append_varint(out, f.stream_id);
    append_varint(out, f.application_error_code);
    return out;
}

auto encode_frame(const crypto_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(16 + f.data.size());
    append_varint(out, 0x06);
    append_varint(out, f.offset);
    append_varint(out, f.data.size());
    append_bytes(out, f.data);
    return out;
}

auto encode_frame(const new_token_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(8 + f.token.size());
    append_varint(out, 0x07);
    append_varint(out, f.token.size());
    append_bytes(out, f.token);
    return out;
}

auto encode_frame(const stream_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(24 + f.data.size());

    // Build type byte with flags
    std::uint64_t type_byte = 0x08;
    if (f.offset > 0)
        type_byte |= 0x04; // OFF bit
    type_byte |= 0x02;     // LEN bit (always set for encoding)
    if (f.fin)
        type_byte |= 0x01; // FIN bit

    append_varint(out, type_byte);
    append_varint(out, f.stream_id);
    if (f.offset > 0)
        append_varint(out, f.offset);
    append_varint(out, f.data.size());
    append_bytes(out, f.data);
    return out;
}

auto encode_frame(const max_data_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(8);
    append_varint(out, 0x10);
    append_varint(out, f.maximum);
    return out;
}

auto encode_frame(const max_stream_data_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(16);
    append_varint(out, 0x11);
    append_varint(out, f.stream_id);
    append_varint(out, f.maximum);
    return out;
}

auto encode_frame(const max_streams_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(8);
    append_varint(out, f.bidirectional ? 0x12 : 0x13);
    append_varint(out, f.maximum);
    return out;
}

auto encode_frame(const data_blocked_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(8);
    append_varint(out, 0x14);
    append_varint(out, f.maximum_data);
    return out;
}

auto encode_frame(const stream_data_blocked_frame& f)
    -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(16);
    append_varint(out, 0x15);
    append_varint(out, f.stream_id);
    append_varint(out, f.maximum_stream_data);
    return out;
}

auto encode_frame(const streams_blocked_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(8);
    append_varint(out, f.bidirectional ? 0x16 : 0x17);
    append_varint(out, f.maximum);
    return out;
}

auto encode_frame(const new_connection_id_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(24 + f.cid.size());
    append_varint(out, 0x18);
    append_varint(out, f.sequence_number);
    append_varint(out, f.retire_prior_to);
    out.push_back(static_cast<std::byte>(f.cid.size()));
    append_bytes(out, {f.cid.data(), f.cid.size()});
    append_bytes(out, f.stateless_reset_token);
    return out;
}

auto encode_frame(const retire_connection_id_frame& f)
    -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(8);
    append_varint(out, 0x19);
    append_varint(out, f.sequence_number);
    return out;
}

auto encode_frame(const path_challenge_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(9);
    out.push_back(static_cast<std::byte>(0x1A));
    append_bytes(out, f.data);
    return out;
}

auto encode_frame(const path_response_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(9);
    out.push_back(static_cast<std::byte>(0x1B));
    append_bytes(out, f.data);
    return out;
}

auto encode_frame(const connection_close_frame& f) -> std::vector<std::byte>
{
    std::vector<std::byte> out;
    out.reserve(24 + f.reason.size());
    append_varint(out, f.is_application_error ? 0x1D : 0x1C);
    append_varint(out, f.error_code);
    if (!f.is_application_error)
        append_varint(out, f.frame_type_value);
    append_varint(out, f.reason.size());
    for (char c : f.reason)
    {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

auto encode_frame(const handshake_done_frame& /*f*/) -> std::vector<std::byte>
{
    return {static_cast<std::byte>(0x1E)};
}

// =============================================================================
// Frame Helpers
// =============================================================================

auto is_ack_eliciting(const quic_frame_variant& f) -> bool
{
    // ACK, PADDING, and CONNECTION_CLOSE are not ack-eliciting
    return !std::holds_alternative<padding_frame>(f) &&
        !std::holds_alternative<ack_frame>(f) &&
        !std::holds_alternative<connection_close_frame>(f);
}

auto is_probing(const quic_frame_variant& f) -> bool
{
    return std::holds_alternative<path_challenge_frame>(f) ||
        std::holds_alternative<path_response_frame>(f) ||
        std::holds_alternative<padding_frame>(f) ||
        std::holds_alternative<new_connection_id_frame>(f);
}

} // namespace cnetmod::quic
