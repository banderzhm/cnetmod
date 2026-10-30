module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.mongodb;

import std;
import :error;
import :bson_document;
import :wire_protocol;

#include "mongodb_wire_integer.hpp"

namespace cnetmod::mongodb {
namespace {
    using wire_detail::append_le;
    using wire_detail::read_le;

    auto crc32c(std::span<const std::byte> bytes) noexcept -> std::uint32_t
    {
        std::uint32_t crc = 0xffffffffu;
        for (auto byte : bytes)
        {
            crc ^= std::to_integer<std::uint8_t>(byte);
            for (int bit{}; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0x82f63b78u & static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1u))));
        }
        return ~crc;
    }

    auto install_sequence(bson_document& body, std::string_view path,
        bson_array documents, std::size_t depth, std::size_t maximum_depth) -> result<void>
    {
        if (path.empty() || depth > maximum_depth)
            return std::unexpected(make_error(error_code::protocol_error,
                "invalid MongoDB document sequence path or nesting depth"));
        const auto dot = path.find('.');
        const auto key = path.substr(0, dot);
        if (key.empty())
            return std::unexpected(make_error(error_code::protocol_error,
                "empty MongoDB document sequence path component"));
        const auto existing = body.find(key);
        if (dot == std::string_view::npos)
        {
            if (existing)
                return std::unexpected(make_error(error_code::protocol_error,
                    "MongoDB document sequence conflicts with body field"));
            body.append(std::string(key), std::move(documents));
            return {};
        }
        if (existing && !existing->as_document())
            return std::unexpected(make_error(error_code::protocol_error,
                "MongoDB document sequence parent is not a document"));
        auto parent = existing ? *existing->as_document() : bson_document{};
        auto installed = install_sequence(parent, path.substr(dot + 1),
            std::move(documents), depth + 1, maximum_depth);
        if (!installed)
            return installed;
        body.set(std::string(key), std::move(parent));
        return {};
    }

    auto unique_body_fields(const bson_document& body) -> bool
    {
        // Commands usually have only a handful of fields. Validate that path
        // without one tree allocation per key; large bodies sort views once.
        if (body.size() <= 16)
        {
            std::array<std::string_view, 16> fields{};
            std::size_t count{};
            for (const auto& [key, value] : body.elements())
            {
                if (std::find(fields.begin(), fields.begin() + count, key) != fields.begin() + count)
                    return false;
                fields[count++] = key;
            }
            return true;
        }
        std::vector<std::string_view> fields;
        fields.reserve(body.size());
        for (const auto& [key, value] : body.elements())
            fields.push_back(key);
        std::ranges::sort(fields);
        return std::ranges::adjacent_find(fields) == fields.end();
    }

} // namespace

auto encode_command_message(std::int32_t request_id,
    const bson_document& command, std::size_t maximum, std::uint32_t flags)
    -> result<std::vector<std::byte>>
{
    return encode_command_message(request_id, command,
        std::span<const document_sequence>{}, maximum, flags);
}

auto encode_command_message(std::int32_t request_id,
    const bson_document& command, std::span<const document_sequence> sequences,
    std::size_t maximum, std::uint32_t flags, bson_limits limits)
    -> result<std::vector<std::byte>>
{
    if ((flags & ~(op_message_exhaust_allowed | op_message_checksum_present)) != 0 ||
        !unique_body_fields(command))
        return std::unexpected(make_error(error_code::protocol_error,
            "invalid MongoDB OP_MSG request flags or duplicate body fields"));
    maximum = std::min(maximum, static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()));
    limits.max_document_bytes = std::min(limits.max_document_bytes, maximum);
    auto encoded = encode_bson_document(command, limits);
    if (!encoded)
        return std::unexpected(encoded.error());
    const auto checksum_size = (flags & op_message_checksum_present) ? std::size_t{4} : 0U;
    if (encoded->size() + 21U + checksum_size > maximum)
        return std::unexpected(make_error(error_code::message_too_large,
            "MongoDB OP_MSG exceeds configured maximum"));
    std::vector<std::byte> out;
    out.reserve(encoded->size() + 21U + checksum_size);
    append_le(out, std::int32_t{}); // Filled once all sections are present.
    append_le(out, request_id);
    append_le(out, std::int32_t{});
    append_le(out, op_message);
    append_le(out, flags);
    out.push_back(std::byte{});
    out.insert(out.end(), encoded->begin(), encoded->end());
    std::optional<bson_document> logical_body;
    if (!sequences.empty())
        logical_body.emplace(command);
    for (const auto& sequence : sequences)
    {
        if (sequence.identifier.find('\0') != std::string::npos)
            return std::unexpected(make_error(error_code::protocol_error,
                "MongoDB document sequence identifier contains NUL"));
        // Validate collisions without copying the potentially large document batch.
        auto valid = install_sequence(*logical_body, sequence.identifier, {}, 1, limits.max_nesting_depth);
        if (!valid)
            return std::unexpected(valid.error());
        if (sequence.identifier.size() + 6U + checksum_size > maximum - out.size())
            return std::unexpected(make_error(error_code::message_too_large,
                "MongoDB document sequence exceeds configured maximum"));
        out.push_back(std::byte{1});
        const auto section_start = out.size();
        append_le(out, std::int32_t{});
        for (const auto character : sequence.identifier)
            out.push_back(static_cast<std::byte>(character));
        out.push_back(std::byte{});
        for (const auto& document : sequence.documents)
        {
            auto bson = encode_bson_document(document, limits);
            if (!bson)
                return std::unexpected(bson.error());
            if (bson->size() + checksum_size > maximum - out.size())
                return std::unexpected(make_error(error_code::message_too_large,
                    "MongoDB document sequence exceeds configured maximum"));
            out.insert(out.end(), bson->begin(), bson->end());
        }
        const auto section_size = static_cast<std::uint32_t>(out.size() - section_start);
        for (std::size_t index{}; index < 4; ++index)
            out[section_start + index] = static_cast<std::byte>((section_size >> (index * 8)) & 0xffU);
    }
    const auto size = static_cast<std::uint32_t>(out.size() + checksum_size);
    for (std::size_t index{}; index < 4; ++index)
        out[index] = static_cast<std::byte>((size >> (index * 8)) & 0xffU);
    if (checksum_size != 0)
        append_le(out, crc32c(out));
    return out;
}

auto decode_message_header(std::span<const std::byte, 16> bytes)
    -> result<message_header>
{
    std::size_t pos{};
    auto length = read_le<std::int32_t>(bytes, pos);
    auto request = read_le<std::int32_t>(bytes, pos);
    auto response = read_le<std::int32_t>(bytes, pos);
    auto opcode = read_le<std::int32_t>(bytes, pos);
    if (!length || !request || !response || !opcode)
        return std::unexpected(make_error(error_code::protocol_error,
            "truncated MongoDB message header"));
    return message_header{*length, *request, *response, *opcode};
}

auto decode_command_message(std::span<const std::byte> bytes,
    std::size_t maximum, bson_limits limits) -> result<decoded_message>
{
    if (bytes.size() < 21 || bytes.size() > maximum)
        return std::unexpected(make_error(error_code::message_too_large,
            "invalid MongoDB OP_MSG size"));
    std::array<std::byte, 16> header_bytes{};
    std::copy_n(bytes.begin(), 16, header_bytes.begin());
    auto header = decode_message_header(header_bytes);
    if (!header)
        return std::unexpected(header.error());
    if (header->message_length != static_cast<std::int32_t>(bytes.size()) ||
        header->operation_code != op_message)
        return std::unexpected(make_error(error_code::protocol_error,
            "unexpected MongoDB opcode or message length"));
    std::size_t pos = 16;
    auto flags = read_le<std::uint32_t>(bytes, pos);
    // Unknown required bits (0..15) change parsing and must fail. Optional
    // bits (16..31) are forward-compatible and do not change the body layout.
    constexpr std::uint32_t required_flags_mask = 0xffffU;
    if (!flags || (*flags & required_flags_mask & ~(op_message_checksum_present | op_message_more_to_come)) != 0)
        return std::unexpected(make_error(error_code::protocol_error,
            "unsupported MongoDB OP_MSG flags"));
    auto payload_end = bytes.size() - ((*flags & op_message_checksum_present) ? 4 : 0);
    if ((*flags & op_message_checksum_present) != 0)
    {
        std::size_t checksum_position = payload_end;
        auto expected_checksum = read_le<std::uint32_t>(bytes, checksum_position);
        if (!expected_checksum || *expected_checksum != crc32c(bytes.first(payload_end)))
            return std::unexpected(make_error(error_code::protocol_error,
                "MongoDB OP_MSG checksum mismatch"));
    }
    limits.max_document_bytes = std::min(limits.max_document_bytes, maximum);
    std::optional<bson_document> body;
    std::vector<std::pair<std::string, bson_array>> sequences;
    while (pos < payload_end)
    {
        const auto kind = bytes[pos++];
        if (kind == std::byte{})
        {
            if (body)
                return std::unexpected(make_error(error_code::protocol_error,
                    "duplicate MongoDB OP_MSG body section"));
            auto length_position = pos;
            auto length = read_le<std::int32_t>(bytes.first(payload_end), length_position);
            if (!length || *length < 5 || static_cast<std::size_t>(*length) > payload_end - pos)
                return std::unexpected(make_error(error_code::protocol_error,
                    "truncated MongoDB OP_MSG body"));
            auto document = decode_bson_document(bytes.subspan(pos, *length), limits);
            if (!document)
                return std::unexpected(document.error());
            if (!unique_body_fields(*document))
                return std::unexpected(make_error(error_code::protocol_error,
                    "duplicate MongoDB OP_MSG body fields"));
            body.emplace(std::move(*document));
            pos += *length;
        }
        else if (kind == std::byte{1})
        {
            const auto section_start = pos;
            auto length = read_le<std::int32_t>(bytes.first(payload_end), pos);
            if (!length || *length < 5 || static_cast<std::size_t>(*length) > payload_end - section_start)
                return std::unexpected(make_error(error_code::protocol_error,
                    "invalid MongoDB document sequence size"));
            const auto end = section_start + *length;
            std::string identifier;
            while (pos < end && bytes[pos] != std::byte{})
                identifier.push_back(static_cast<char>(bytes[pos++]));
            if (pos == end)
                return std::unexpected(make_error(error_code::protocol_error,
                    "unterminated MongoDB document sequence identifier"));
            ++pos;
            bson_array documents;
            while (pos < end)
            {
                auto length_position = pos;
                auto document_length = read_le<std::int32_t>(bytes.first(end), length_position);
                if (!document_length || *document_length < 5 ||
                    static_cast<std::size_t>(*document_length) > end - pos)
                    return std::unexpected(make_error(error_code::protocol_error,
                        "truncated MongoDB document sequence BSON"));
                auto document = decode_bson_document(bytes.subspan(pos, *document_length), limits);
                if (!document)
                    return std::unexpected(document.error());
                documents.emplace_back(std::move(*document));
                pos += *document_length;
            }
            sequences.emplace_back(std::move(identifier), std::move(documents));
        }
        else
            return std::unexpected(make_error(error_code::protocol_error,
                "unknown MongoDB OP_MSG section kind"));
    }
    if (!body)
        return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB OP_MSG requires exactly one body section"));
    for (auto& [identifier, documents] : sequences)
    {
        auto installed = install_sequence(*body, identifier, std::move(documents), 1, limits.max_nesting_depth);
        if (!installed)
            return std::unexpected(installed.error());
    }
    return decoded_message{*header, *flags, std::move(*body)};
}

} // namespace cnetmod::mongodb
