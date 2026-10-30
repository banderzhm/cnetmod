module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
    #include <zlib.h>
#endif
#ifdef CNETMOD_HAS_ZSTD
    #include <zstd.h>
#endif
#ifdef CNETMOD_HAS_SNAPPY
    #include <snappy-c.h>
#endif
module cnetmod.protocol.mongodb;
import std;
import :error;
import :wire_protocol;
#include "mongodb_wire_integer.hpp"

namespace cnetmod::mongodb {
using wire_detail::append_le;
using wire_detail::read_le;

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
    if (!header || header->operation_code == op_compressed ||
        message.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
        header->message_length != static_cast<std::int32_t>(message.size()))
        return std::unexpected(make_error(error_code::protocol_error,
            "invalid MongoDB source header or nested OP_COMPRESSED"));
    auto payload = message.subspan(16);
    std::vector<std::byte> compressed;
    if (compressor_id == compressor_noop)
        compressed.assign(payload.begin(), payload.end());
    else if (compressor_id == compressor_snappy)
    {
#ifdef CNETMOD_HAS_SNAPPY
        std::size_t size = snappy_max_compressed_length(payload.size());
        compressed.resize(size);
        if (snappy_compress(reinterpret_cast<const char*>(payload.data()), payload.size(),
                reinterpret_cast<char*>(compressed.data()), &size) != SNAPPY_OK)
            return std::unexpected(make_error(error_code::compression_failed, "MongoDB Snappy compression failed"));
        compressed.resize(size);
#else
        return std::unexpected(make_error(error_code::compression_failed, "MongoDB Snappy codec is unavailable"));
#endif
    }
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
    else if (compressor_id == compressor_zstd)
    {
#ifdef CNETMOD_HAS_ZSTD
        compressed.resize(ZSTD_compressBound(payload.size()));
        const auto size = ZSTD_compress(compressed.data(), compressed.size(),
            payload.data(), payload.size(), 3);
        if (ZSTD_isError(size))
            return std::unexpected(make_error(error_code::compression_failed,
                "zstd failed to compress MongoDB message"));
        compressed.resize(size);
#else
        return std::unexpected(make_error(error_code::compression_failed,
            "MongoDB zstd compression requested but zstd support is unavailable"));
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
        *uncompressed_size < 0 ||
        *uncompressed_size > std::numeric_limits<std::int32_t>::max() - 16 ||
        static_cast<std::size_t>(*uncompressed_size) > maximum - 16 ||
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
    else if (compressor == compressor_snappy)
    {
#ifdef CNETMOD_HAS_SNAPPY
        std::size_t size{};
        if (snappy_uncompressed_length(reinterpret_cast<const char*>(compressed.data()), compressed.size(), &size) != SNAPPY_OK ||
            size != payload.size() || snappy_uncompress(reinterpret_cast<const char*>(compressed.data()), compressed.size(), reinterpret_cast<char*>(payload.data()), &size) != SNAPPY_OK || size != payload.size())
            return std::unexpected(make_error(error_code::compression_failed, "invalid MongoDB Snappy payload"));
#else
        return std::unexpected(make_error(error_code::compression_failed, "MongoDB Snappy codec is unavailable"));
#endif
    }
    else if (compressor == compressor_zlib)
    {
#ifdef CNETMOD_HAS_ZLIB
        uLongf output_size = static_cast<uLongf>(payload.size());
        uLong input_size = static_cast<uLong>(compressed.size());
        auto status = uncompress2(reinterpret_cast<Bytef*>(payload.data()), &output_size,
            reinterpret_cast<const Bytef*>(compressed.data()), &input_size);
        if (status != Z_OK || output_size != payload.size() || input_size != compressed.size())
            return std::unexpected(make_error(error_code::compression_failed,
                "invalid MongoDB zlib payload"));
#else
        return std::unexpected(make_error(error_code::compression_failed,
            "received MongoDB zlib payload but zlib support is unavailable"));
#endif
    }
    else if (compressor == compressor_zstd)
    {
#ifdef CNETMOD_HAS_ZSTD
        const auto frame_size = ZSTD_findFrameCompressedSize(compressed.data(), compressed.size());
        if (ZSTD_isError(frame_size) || frame_size != compressed.size())
            return std::unexpected(make_error(error_code::compression_failed,
                "invalid MongoDB zstd frame boundary"));
        const auto size = ZSTD_decompress(payload.data(), payload.size(),
            compressed.data(), compressed.size());
        if (ZSTD_isError(size) || size != payload.size())
            return std::unexpected(make_error(error_code::compression_failed,
                "invalid MongoDB zstd payload"));
#else
        return std::unexpected(make_error(error_code::compression_failed,
            "received MongoDB zstd payload but zstd support is unavailable"));
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
