module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :codec_message;

import std;
import :types;
import :codec;

namespace cnetmod::coap {
namespace {

auto decode_extended(std::uint8_t nibble, std::span<const std::byte> bytes,
                     std::size_t& offset)
    -> std::expected<std::uint16_t, std::error_code>
{
    if (nibble < 13) {
        return nibble;
    }
    if (nibble == 13) {
        if (offset >= bytes.size()) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        return static_cast<std::uint16_t>(13 + std::to_integer<std::uint8_t>(bytes[offset++]));
    }
    if (nibble == 14) {
        if (offset + 1 >= bytes.size()) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        const auto hi = std::to_integer<std::uint16_t>(bytes[offset++]);
        const auto lo = std::to_integer<std::uint16_t>(bytes[offset++]);
        return static_cast<std::uint16_t>(269 + ((hi << 8) | lo));
    }
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
}

auto encode_extended(std::uint16_t value, std::vector<std::byte>& out) -> std::uint8_t {
    if (value < 13) {
        return static_cast<std::uint8_t>(value);
    }
    if (value < 269) {
        out.push_back(static_cast<std::byte>(value - 13));
        return 13;
    }
    const auto ext = static_cast<std::uint16_t>(value - 269);
    out.push_back(static_cast<std::byte>((ext >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(ext & 0xff));
    return 14;
}

} // namespace

auto parse_message(std::span<const std::byte> bytes)
    -> std::expected<message, std::error_code>
{
    if (bytes.size() < 4) {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    const auto first = std::to_integer<std::uint8_t>(bytes[0]);
    const auto version = static_cast<std::uint8_t>(first >> 6);
    if (version != 1) {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    const auto token_length = static_cast<std::size_t>(first & 0x0f);
    if (token_length > max_token_size || bytes.size() < 4 + token_length) {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    message msg;
    msg.type = static_cast<message_type>((first >> 4) & 0x03);
    msg.code = std::to_integer<std::uint8_t>(bytes[1]);
    msg.message_id =
        (std::to_integer<std::uint16_t>(bytes[2]) << 8) |
        std::to_integer<std::uint16_t>(bytes[3]);

    if (msg.code == 0 && token_length != 0) {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    std::size_t offset = 4;
    msg.token.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + token_length));
    offset += token_length;

    std::uint16_t previous_option = 0;
    while (offset < bytes.size()) {
        if (std::to_integer<std::uint8_t>(bytes[offset]) == 0xff) {
            ++offset;
            if (offset >= bytes.size()) {
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
            msg.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
            return msg;
        }

        const auto opt_header = std::to_integer<std::uint8_t>(bytes[offset++]);
        auto delta = decode_extended(static_cast<std::uint8_t>(opt_header >> 4), bytes, offset);
        if (!delta) {
            return std::unexpected(delta.error());
        }
        auto length = decode_extended(static_cast<std::uint8_t>(opt_header & 0x0f), bytes, offset);
        if (!length) {
            return std::unexpected(length.error());
        }
        if (offset + *length > bytes.size()) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        const auto number = static_cast<std::uint16_t>(previous_option + *delta);
        previous_option = number;
        msg.options.push_back(option{
            .number = number,
            .value = {bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                      bytes.begin() + static_cast<std::ptrdiff_t>(offset + *length)}
        });
        offset += *length;
    }

    return msg;
}

auto serialize_message(const message& msg)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (msg.token.size() > max_token_size) {
        return std::unexpected(std::make_error_code(std::errc::message_size));
    }
    if (msg.code == 0 && (!msg.token.empty() || !msg.options.empty() || !msg.payload.empty())) {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    std::vector<option> sorted_options = msg.options;
    std::stable_sort(sorted_options.begin(), sorted_options.end(),
        [](const option& lhs, const option& rhs) {
            return lhs.number < rhs.number;
        });

    std::vector<std::byte> out;
    out.reserve(4 + msg.token.size() + msg.payload.size() + sorted_options.size() * 4);
    const auto first = static_cast<std::uint8_t>(
        (1u << 6) |
        ((static_cast<std::uint8_t>(msg.type) & 0x03u) << 4) |
        (static_cast<std::uint8_t>(msg.token.size()) & 0x0fu));
    out.push_back(static_cast<std::byte>(first));
    out.push_back(static_cast<std::byte>(msg.code));
    out.push_back(static_cast<std::byte>((msg.message_id >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(msg.message_id & 0xff));
    out.insert(out.end(), msg.token.begin(), msg.token.end());

    std::uint16_t previous_option = 0;
    for (const auto& opt : sorted_options) {
        if (opt.number < previous_option) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        if (opt.value.size() > std::numeric_limits<std::uint16_t>::max()) {
            return std::unexpected(std::make_error_code(std::errc::message_size));
        }

        std::vector<std::byte> delta_ext;
        std::vector<std::byte> len_ext;
        const auto delta_nibble = encode_extended(
            static_cast<std::uint16_t>(opt.number - previous_option), delta_ext);
        const auto len_nibble = encode_extended(
            static_cast<std::uint16_t>(opt.value.size()), len_ext);
        out.push_back(static_cast<std::byte>((delta_nibble << 4) | len_nibble));
        out.insert(out.end(), delta_ext.begin(), delta_ext.end());
        out.insert(out.end(), len_ext.begin(), len_ext.end());
        out.insert(out.end(), opt.value.begin(), opt.value.end());
        previous_option = opt.number;
    }

    if (!msg.payload.empty()) {
        out.push_back(static_cast<std::byte>(0xff));
        out.insert(out.end(), msg.payload.begin(), msg.payload.end());
    }

    return out;
}

} // namespace cnetmod::coap
