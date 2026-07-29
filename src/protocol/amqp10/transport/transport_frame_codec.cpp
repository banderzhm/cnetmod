module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.amqp10;
import :transport_frame_codec;

import std;
import :amqp_value_codec;

namespace cnetmod::amqp10 {
auto encode_protocol_header(protocol_header h) -> binary
{
    return {std::byte{'A'},
        std::byte{'M'},
        std::byte{'Q'},
        std::byte{'P'},
        std::byte(static_cast<std::uint8_t>(h.protocol)),
        std::byte(h.major),
        std::byte(h.minor),
        std::byte(h.revision)};
}

auto decode_protocol_header(std::span<const std::byte> b)
    -> std::expected<protocol_header, std::error_code>
{
    if (b.size() != 8 || b[0] != std::byte{'A'} || b[1] != std::byte{'M'} ||
        b[2] != std::byte{'Q'} || b[3] != std::byte{'P'})
        return std::unexpected(make_error_code(errc::malformed_frame));
    return protocol_header{
        static_cast<protocol_id>(std::to_integer<std::uint8_t>(b[4])),
        std::to_integer<std::uint8_t>(b[5]), std::to_integer<std::uint8_t>(b[6]),
        std::to_integer<std::uint8_t>(b[7])};
}

auto encode_frame(const frame& f) -> binary
{
    encoder out;
    out.write_u32(static_cast<std::uint32_t>(8 + f.body.size()));
    out.write_u8(2);
    out.write_u8(static_cast<std::uint8_t>(f.type));
    out.write_u16(f.channel);
    out.write_bytes(f.body);
    return out.release();
}

auto decode_frame(std::span<const std::byte> bytes, std::uint32_t maximum)
    -> std::expected<frame, std::error_code>
{
    if (bytes.size() < 8)
        return std::unexpected(make_error_code(errc::frame_size_too_small));
    decoder in(bytes);
    auto size = in.read_u32();
    auto doff = in.read_u8();
    auto type = in.read_u8();
    auto channel = in.read_u16();
    if (!size || !doff || !type || !channel)
        return std::unexpected(make_error_code(errc::malformed_frame));
    if (*size < 8 || *doff < 2 || std::uint32_t(*doff) * 4 > *size)
        return std::unexpected(make_error_code(errc::frame_size_too_small));
    if (*size > maximum || *size > bytes.size())
        return std::unexpected(make_error_code(errc::frame_size_too_large));
    const auto body_offset = std::size_t(*doff) * 4;
    return frame{static_cast<frame_type>(*type), *channel,
        binary(bytes.begin() + static_cast<std::ptrdiff_t>(body_offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(*size))};
}
} // namespace cnetmod::amqp10
