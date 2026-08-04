module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.v3.frame;
import std;
import cnetmod.core.buffer;

namespace cnetmod::http::v3 {
namespace {

    auto encode_varint(std::uint64_t value, byte_buffer& out) -> void
    {
        if (value <= 63U)
        {
            out.push_back(static_cast<std::byte>(value));
        }
        else if (value <= 16383U)
        {
            out.push_back(static_cast<std::byte>((value >> 8U) | 0x40U));
            out.push_back(static_cast<std::byte>(value));
        }
        else if (value <= 1073741823U)
        {
            out.push_back(static_cast<std::byte>((value >> 24U) | 0x80U));
            out.push_back(static_cast<std::byte>(value >> 16U));
            out.push_back(static_cast<std::byte>(value >> 8U));
            out.push_back(static_cast<std::byte>(value));
        }
        else if (value <= 4611686018427387903ULL)
        {
            out.push_back(static_cast<std::byte>((value >> 56U) | 0xc0U));
            for (auto shift = 48; shift >= 0; shift -= 8)
                out.push_back(static_cast<std::byte>(value >> shift));
        }
    }

    auto decode_varint(byte_view input, std::size_t& used)
        -> std::expected<std::uint64_t, std::error_code>
    {
        if (input.empty())
            return std::unexpected(std::make_error_code(std::errc::message_size));
        const auto first = std::to_integer<std::uint8_t>(input.front());
        const auto length = static_cast<std::size_t>(1U << (first >> 6U));
        if (input.size() < length)
            return std::unexpected(std::make_error_code(std::errc::message_size));
        std::uint64_t value = first & 0x3fU;
        for (std::size_t index = 1; index < length; ++index)
            value = (value << 8U) | std::to_integer<std::uint8_t>(input[index]);
        used = length;
        return value;
    }

    auto encode_frame(std::uint64_t type, byte_view payload) -> byte_buffer
    {
        byte_buffer result;
        result.reserve(16U + payload.size());
        encode_varint(type, result);
        encode_varint(payload.size(), result);
        result.append(payload);
        return result;
    }

    auto varint_payload(std::uint64_t value) -> byte_buffer
    {
        byte_buffer result;
        encode_varint(value, result);
        return result;
    }

    auto decode_single_varint(byte_view payload)
        -> std::expected<std::uint64_t, std::error_code>
    {
        std::size_t used{};
        auto value = decode_varint(payload, used);
        if (!value || used != payload.size())
            return std::unexpected(value ? std::make_error_code(std::errc::invalid_argument) : value.error());
        return *value;
    }

} // namespace

auto decode_http3_frame(byte_view input)
    -> std::expected<std::pair<http3_frame_variant, std::size_t>, std::error_code>
{
    std::size_t type_size{};
    auto type = decode_varint(input, type_size);
    if (!type)
        return std::unexpected(type.error());
    std::size_t length_size{};
    auto length = decode_varint(input.subspan(type_size), length_size);
    if (!length)
        return std::unexpected(length.error());
    const auto header_size = type_size + length_size;
    if (*length > input.size() - header_size)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    const auto payload = input.subspan(header_size, static_cast<std::size_t>(*length));
    const auto consumed = header_size + payload.size();

    switch (static_cast<http3_frame_type>(*type))
    {
    case http3_frame_type::data:
        return std::pair{http3_frame_variant{data_frame{payload}}, consumed};
    case http3_frame_type::headers:
        return std::pair{http3_frame_variant{headers_frame{payload}}, consumed};
    case http3_frame_type::cancel_push:
    {
        auto push_id = decode_single_varint(payload);
        if (!push_id)
            return std::unexpected(push_id.error());
        return std::pair{http3_frame_variant{cancel_push_frame{*push_id}}, consumed};
    }
    case http3_frame_type::settings:
    {
        settings_frame settings;
        std::size_t offset{};
        while (offset < payload.size())
        {
            std::size_t key_size{};
            auto key = decode_varint(payload.subspan(offset), key_size);
            if (!key)
                return std::unexpected(key.error());
            offset += key_size;
            std::size_t value_size{};
            auto value = decode_varint(payload.subspan(offset), value_size);
            if (!value)
                return std::unexpected(value.error());
            offset += value_size;
            if (!settings.settings.emplace(*key, *value).second)
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        return std::pair{http3_frame_variant{std::move(settings)}, consumed};
    }
    case http3_frame_type::push_promise:
    {
        std::size_t id_size{};
        auto push_id = decode_varint(payload, id_size);
        if (!push_id)
            return std::unexpected(push_id.error());
        return std::pair{http3_frame_variant{push_promise_frame{*push_id, payload.subspan(id_size)}}, consumed};
    }
    case http3_frame_type::goaway:
    {
        auto id = decode_single_varint(payload);
        if (!id)
            return std::unexpected(id.error());
        return std::pair{http3_frame_variant{goaway_frame{*id, {}, {}}}, consumed};
    }
    case http3_frame_type::max_push_id:
    {
        auto push_id = decode_single_varint(payload);
        if (!push_id)
            return std::unexpected(push_id.error());
        return std::pair{http3_frame_variant{max_push_id_frame{*push_id}}, consumed};
    }
    default:
        return std::pair{http3_frame_variant{unknown_frame{*type, payload}}, consumed};
    }
}

auto encode_http3_frame(const data_frame& frame) -> byte_buffer
{
    return encode_frame(static_cast<std::uint64_t>(http3_frame_type::data), frame.data);
}

auto encode_http3_frame(const headers_frame& frame) -> byte_buffer
{
    return encode_frame(static_cast<std::uint64_t>(http3_frame_type::headers), frame.encoded_headers);
}

auto encode_http3_frame(const settings_frame& frame) -> byte_buffer
{
    byte_buffer payload;
    for (const auto& [key, value] : frame.settings)
    {
        if (!std::holds_alternative<std::uint64_t>(value))
            return {};
        encode_varint(key, payload);
        encode_varint(std::get<std::uint64_t>(value), payload);
    }
    return encode_frame(static_cast<std::uint64_t>(http3_frame_type::settings), payload);
}

auto encode_http3_frame(const push_promise_frame& frame) -> byte_buffer
{
    auto payload = varint_payload(frame.promised_stream_id);
    payload.append(frame.encoded_headers);
    return encode_frame(static_cast<std::uint64_t>(http3_frame_type::push_promise), payload);
}

auto encode_http3_frame(const goaway_frame& frame) -> byte_buffer
{
    // RFC 9114 GOAWAY contains exactly one Push ID or client-initiated stream ID.
    return encode_frame(static_cast<std::uint64_t>(http3_frame_type::goaway), varint_payload(frame.stream_id));
}

auto encode_http3_frame(const max_push_id_frame& frame) -> byte_buffer
{
    return encode_frame(static_cast<std::uint64_t>(http3_frame_type::max_push_id), varint_payload(frame.max_push_id));
}

auto encode_http3_frame(const cancel_push_frame& frame) -> byte_buffer
{
    return encode_frame(static_cast<std::uint64_t>(http3_frame_type::cancel_push), varint_payload(frame.push_id));
}

auto encode_http3_frame(const http3_frame_variant& frame) -> byte_buffer
{
    return std::visit([](const auto& value) -> byte_buffer
        {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, unknown_frame>)
                return encode_frame(value.type, value.payload);
            else
                return encode_http3_frame(value);
        },
        frame);
}

auto is_stream_frame(const http3_frame_variant& frame) noexcept -> bool
{
    return std::holds_alternative<data_frame>(frame) || std::holds_alternative<headers_frame>(frame) ||
        std::holds_alternative<push_promise_frame>(frame);
}

} // namespace cnetmod::http::v3
