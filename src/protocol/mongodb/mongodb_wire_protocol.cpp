module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
    #include <zlib.h>
#endif
module cnetmod.protocol.mongodb;

import std;
import :error;
import :bson_document;
import :wire_protocol;

namespace cnetmod::mongodb {
namespace {
    template <class T>
    void append_le(std::vector<std::byte>& out, T value)
    {
        using U = std::make_unsigned_t<T>;
        auto bits = static_cast<U>(value);
        for (std::size_t i{}; i < sizeof(T); ++i)
            out.push_back(static_cast<std::byte>((bits >> (i * 8)) & 0xff));
    }

    template <class T>
    auto read_le(std::span<const std::byte> bytes, std::size_t& pos) -> std::optional<T>
    {
        if (bytes.size() - pos < sizeof(T))
            return {};
        using U = std::make_unsigned_t<T>;
        U bits{};
        for (std::size_t i{}; i < sizeof(T); ++i)
            bits |= static_cast<U>(std::to_integer<unsigned>(bytes[pos++])) << (8 * i);
        return static_cast<T>(bits);
    }

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
} // namespace

auto encode_command_message(std::int32_t request_id,
    const bson_document& command, std::size_t maximum)
    -> result<std::vector<std::byte>>
{
    auto encoded = encode_bson_document(command,
        bson_limits{.max_document_bytes = std::min(maximum, std::size_t{16 * 1024 * 1024})});
    if (!encoded)
        return std::unexpected(encoded.error());
    auto size = std::size_t{16 + 4 + 1} + encoded->size();
    if (size > maximum || size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::unexpected(make_error(error_code::message_too_large,
            "MongoDB OP_MSG exceeds configured maximum"));
    std::vector<std::byte> out;
    out.reserve(size);
    append_le(out, static_cast<std::int32_t>(size));
    append_le(out, request_id);
    append_le(out, std::int32_t{});
    append_le(out, op_message);
    append_le(out, std::uint32_t{});
    out.push_back(std::byte{}); // kind 0: single body
    out.insert(out.end(), encoded->begin(), encoded->end());
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
    if (!flags || (*flags & ~(op_message_checksum_present | op_message_more_to_come)) != 0)
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
    if (pos >= payload_end || bytes[pos++] != std::byte{})
        return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB response does not start with an OP_MSG body section"));
    if (payload_end - pos < 5)
        return std::unexpected(make_error(error_code::protocol_error,
            "truncated MongoDB OP_MSG body"));
    std::size_t length_position = pos;
    auto document_length = read_le<std::int32_t>(bytes, length_position);
    if (!document_length || *document_length < 5 ||
        static_cast<std::size_t>(*document_length) != payload_end - pos)
        return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB OP_MSG has unsupported sections or invalid body length"));
    limits.max_document_bytes = std::min(limits.max_document_bytes, maximum);
    auto document = decode_bson_document(bytes.subspan(pos, *document_length), limits);
    if (!document)
        return std::unexpected(document.error());
    return decoded_message{*header, *flags, std::move(*document)};
}

auto encode_compressed_message(std::span<const std::byte> message,
    std::uint8_t compressor_id, std::size_t maximum)
    -> result<std::vector<std::byte>>
{
    if (message.size() < 16 || message.size() > maximum)
        return std::unexpected(make_error(error_code::protocol_error,
            "cannot compress an invalid MongoDB wire message"));
    std::array<std::byte, 16> raw_header{};
    std::copy_n(message.begin(), 16, raw_header.begin());
    auto header = decode_message_header(raw_header);
    if (!header || header->operation_code == op_compressed)
        return std::unexpected(make_error(error_code::protocol_error,
            "nested MongoDB OP_COMPRESSED is forbidden"));
    auto payload = message.subspan(16);
    std::vector<std::byte> compressed;
    if (compressor_id == compressor_noop)
        compressed.assign(payload.begin(), payload.end());
    else if (compressor_id == compressor_zlib)
    {
#ifdef CNETMOD_HAS_ZLIB
        uLongf bound = compressBound(static_cast<uLong>(payload.size()));
        compressed.resize(bound);
        auto status = compress2(reinterpret_cast<Bytef*>(compressed.data()), &bound,
            reinterpret_cast<const Bytef*>(payload.data()), static_cast<uLong>(payload.size()),
            Z_DEFAULT_COMPRESSION);
        if (status != Z_OK)
            return std::unexpected(make_error(error_code::compression_failed,
                "zlib failed to compress MongoDB message"));
        compressed.resize(bound);
#else
        return std::unexpected(make_error(error_code::compression_failed,
            "MongoDB zlib compression requested but zlib support is unavailable"));
#endif
    }
    else
        return std::unexpected(make_error(error_code::compression_failed,
            "unsupported MongoDB compressor id"));
    const auto total = std::size_t{25} + compressed.size();
    if (total > maximum || total > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::unexpected(make_error(error_code::message_too_large,
            "compressed MongoDB message exceeds configured maximum"));
    std::vector<std::byte> output;
    output.reserve(total);
    append_le(output, static_cast<std::int32_t>(total));
    append_le(output, header->request_id);
    append_le(output, header->response_to);
    append_le(output, op_compressed);
    append_le(output, header->operation_code);
    append_le(output, static_cast<std::int32_t>(payload.size()));
    output.push_back(static_cast<std::byte>(compressor_id));
    output.insert(output.end(), compressed.begin(), compressed.end());
    return output;
}

auto decode_compressed_message(std::span<const std::byte> message,
    std::size_t maximum) -> result<std::vector<std::byte>>
{
    if (message.size() < 25 || message.size() > maximum)
        return std::unexpected(make_error(error_code::protocol_error,
            "invalid MongoDB OP_COMPRESSED length"));
    std::array<std::byte, 16> raw_header{};
    std::copy_n(message.begin(), 16, raw_header.begin());
    auto header = decode_message_header(raw_header);
    if (!header || header->operation_code != op_compressed ||
        header->message_length != static_cast<std::int32_t>(message.size()))
        return std::unexpected(make_error(error_code::protocol_error,
            "invalid MongoDB OP_COMPRESSED header"));
    std::size_t pos = 16;
    auto original_opcode = read_le<std::int32_t>(message, pos);
    auto uncompressed_size = read_le<std::int32_t>(message, pos);
    if (!original_opcode || *original_opcode == op_compressed || !uncompressed_size ||
        *uncompressed_size < 0 || static_cast<std::size_t>(*uncompressed_size) > maximum - 16 ||
        pos >= message.size())
        return std::unexpected(make_error(error_code::protocol_error,
            "invalid MongoDB OP_COMPRESSED metadata"));
    auto compressor = std::to_integer<std::uint8_t>(message[pos++]);
    auto compressed = message.subspan(pos);
    std::vector<std::byte> payload(static_cast<std::size_t>(*uncompressed_size));
    if (compressor == compressor_noop)
    {
        if (compressed.size() != payload.size())
            return std::unexpected(make_error(error_code::compression_failed,
                "MongoDB noop compressed size mismatch"));
        std::copy(compressed.begin(), compressed.end(), payload.begin());
    }
    else if (compressor == compressor_zlib)
    {
#ifdef CNETMOD_HAS_ZLIB
        uLongf output_size = static_cast<uLongf>(payload.size());
        auto status = uncompress(reinterpret_cast<Bytef*>(payload.data()), &output_size,
            reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressed.size()));
        if (status != Z_OK || output_size != payload.size())
            return std::unexpected(make_error(error_code::compression_failed,
                "invalid MongoDB zlib payload"));
#else
        return std::unexpected(make_error(error_code::compression_failed,
            "received MongoDB zlib payload but zlib support is unavailable"));
#endif
    }
    else
        return std::unexpected(make_error(error_code::compression_failed,
            "unsupported MongoDB compressor id"));
    std::vector<std::byte> output;
    output.reserve(16 + payload.size());
    append_le(output, static_cast<std::int32_t>(16 + payload.size()));
    append_le(output, header->request_id);
    append_le(output, header->response_to);
    append_le(output, *original_opcode);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

} // namespace cnetmod::mongodb
