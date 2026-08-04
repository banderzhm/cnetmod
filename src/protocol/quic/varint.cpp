module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :varint;

namespace cnetmod::quic {

auto decode_varint(std::span<const std::byte> data)
    -> std::expected<std::pair<std::uint64_t, std::size_t>, std::error_code>
{
    if (data.empty())
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    const auto first = std::to_integer<std::uint8_t>(data[0]);
    const auto prefix = (first >> 6) & 0x03;

    // length: 1, 2, 4, or 8 bytes based on top 2 bits
    const std::size_t length = std::size_t{1} << prefix;

    if (data.size() < length)
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));

    std::uint64_t value = static_cast<std::uint64_t>(first & 0x3f);
    for (std::size_t i = 1; i < length; ++i)
    {
        value = (value << 8) | std::to_integer<std::uint64_t>(data[i]);
    }

    // Reject non-canonical encodings: value must actually need `length` bytes
    if (length > 1 && value < (std::uint64_t{1} << (8 * (length / 2) - 2)))
    {
        // Non-canonical but still valid in QUIC spec (RFC 9000 allows it)
        // We accept it per spec.
    }

    if (value > max_varint_value)
        return std::unexpected(
            std::make_error_code(std::errc::value_too_large));

    return std::pair{value, length};
}

auto encode_varint(std::uint64_t value)
    -> std::expected<std::pair<std::array<std::byte, 8>, std::size_t>,
        std::error_code>
{
    if (value > max_varint_value)
        return std::unexpected(
            std::make_error_code(std::errc::value_too_large));

    std::array<std::byte, 8> result{};
    const auto len = varint_size(value);

    switch (len)
    {
    case 1:
        result[0] = static_cast<std::byte>(value);
        break;
    case 2:
        result[0] = static_cast<std::byte>(0x40 | (value >> 8));
        result[1] = static_cast<std::byte>(value & 0xff);
        break;
    case 4:
        result[0] = static_cast<std::byte>(0x80 | (value >> 24));
        result[1] = static_cast<std::byte>((value >> 16) & 0xff);
        result[2] = static_cast<std::byte>((value >> 8) & 0xff);
        result[3] = static_cast<std::byte>(value & 0xff);
        break;
    case 8:
        result[0] = static_cast<std::byte>(0xc0 | (value >> 56));
        result[1] = static_cast<std::byte>((value >> 48) & 0xff);
        result[2] = static_cast<std::byte>((value >> 40) & 0xff);
        result[3] = static_cast<std::byte>((value >> 32) & 0xff);
        result[4] = static_cast<std::byte>((value >> 24) & 0xff);
        result[5] = static_cast<std::byte>((value >> 16) & 0xff);
        result[6] = static_cast<std::byte>((value >> 8) & 0xff);
        result[7] = static_cast<std::byte>(value & 0xff);
        break;
    }

    return std::pair{result, static_cast<std::size_t>(len)};
}

auto encode_varint_to(std::uint64_t value, std::span<std::byte> output)
    -> std::expected<std::size_t, std::error_code>
{
    if (value > max_varint_value)
        return std::unexpected(
            std::make_error_code(std::errc::value_too_large));

    const auto len = varint_size(value);
    if (output.size() < len)
        return std::unexpected(
            std::make_error_code(std::errc::no_buffer_space));

    switch (len)
    {
    case 1:
        output[0] = static_cast<std::byte>(value);
        break;
    case 2:
        output[0] = static_cast<std::byte>(0x40 | (value >> 8));
        output[1] = static_cast<std::byte>(value & 0xff);
        break;
    case 4:
        output[0] = static_cast<std::byte>(0x80 | (value >> 24));
        output[1] = static_cast<std::byte>((value >> 16) & 0xff);
        output[2] = static_cast<std::byte>((value >> 8) & 0xff);
        output[3] = static_cast<std::byte>(value & 0xff);
        break;
    case 8:
        output[0] = static_cast<std::byte>(0xc0 | (value >> 56));
        output[1] = static_cast<std::byte>((value >> 48) & 0xff);
        output[2] = static_cast<std::byte>((value >> 40) & 0xff);
        output[3] = static_cast<std::byte>((value >> 32) & 0xff);
        output[4] = static_cast<std::byte>((value >> 24) & 0xff);
        output[5] = static_cast<std::byte>((value >> 16) & 0xff);
        output[6] = static_cast<std::byte>((value >> 8) & 0xff);
        output[7] = static_cast<std::byte>(value & 0xff);
        break;
    }

    return len;
}

} // namespace cnetmod::quic
