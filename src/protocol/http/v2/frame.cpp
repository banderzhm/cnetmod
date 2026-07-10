module cnetmod.protocol.http.v2.frame;

import std;

namespace cnetmod::http::v2 {

auto decode_frame_header(std::span<const std::byte> input)
    -> std::expected<frame_header, std::error_code>
{
    if (input.size() < frame_header_size)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    frame_header header{};
    header.length = (std::to_integer<std::uint32_t>(input[0]) << 16) |
                    (std::to_integer<std::uint32_t>(input[1]) << 8) |
                    std::to_integer<std::uint32_t>(input[2]);
    header.type = static_cast<frame_type>(std::to_integer<std::uint8_t>(input[3]));
    header.flags = std::to_integer<std::uint8_t>(input[4]);
    header.stream_id = (std::to_integer<std::uint32_t>(input[5]) << 24) |
                       (std::to_integer<std::uint32_t>(input[6]) << 16) |
                       (std::to_integer<std::uint32_t>(input[7]) << 8) |
                       std::to_integer<std::uint32_t>(input[8]);
    header.stream_id &= 0x7fff'ffffU;
    if (header.length > max_frame_payload)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    return header;
}

auto encode_frame_header(frame_header header)
    -> std::array<std::byte, frame_header_size>
{
    header.length = std::min(header.length, max_frame_payload);
    header.stream_id &= 0x7fff'ffffU;
    return {
        static_cast<std::byte>(header.length >> 16),
        static_cast<std::byte>(header.length >> 8),
        static_cast<std::byte>(header.length),
        static_cast<std::byte>(header.type),
        static_cast<std::byte>(header.flags),
        static_cast<std::byte>(header.stream_id >> 24),
        static_cast<std::byte>(header.stream_id >> 16),
        static_cast<std::byte>(header.stream_id >> 8),
        static_cast<std::byte>(header.stream_id),
    };
}

} // namespace cnetmod::http::v2
