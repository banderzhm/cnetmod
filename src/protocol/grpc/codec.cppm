module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
#include <zlib.h>
#endif

export module cnetmod.protocol.grpc.codec;

import std;
import cnetmod.core.error;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc {

export struct codec_options {
    compression_algorithm compression = compression_algorithm::identity;
    bool accept_compressed = false;
    std::size_t max_message_bytes = default_max_message_bytes;
};

namespace detail {

inline void append_frame(byte_buffer& out, std::span<const std::byte> payload, bool compressed) {
    auto n = static_cast<std::uint32_t>(payload.size());
    out.push_back(compressed ? std::byte{1} : std::byte{0});
    out.push_back(static_cast<std::byte>((n >> 24) & 0xff));
    out.push_back(static_cast<std::byte>((n >> 16) & 0xff));
    out.push_back(static_cast<std::byte>((n >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(n & 0xff));
    out.insert(out.end(), payload.begin(), payload.end());
}

inline auto gzip_compress(std::span<const std::byte> payload)
    -> std::expected<byte_buffer, status>
{
#ifdef CNETMOD_HAS_ZLIB
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return std::unexpected(make_status(status_code::internal, "gzip deflate init failed"));
    }

    byte_buffer out;
    out.resize(std::max<std::size_t>(128, compressBound(static_cast<uLong>(payload.size()))));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(payload.data()));
    stream.avail_in = static_cast<uInt>(payload.size());

    int rc = Z_OK;
    do {
        if (stream.total_out == out.size()) out.resize(out.size() * 2);
        stream.next_out = reinterpret_cast<Bytef*>(out.data() + stream.total_out);
        stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
        rc = deflate(&stream, Z_FINISH);
    } while (rc == Z_OK);

    if (rc != Z_STREAM_END) {
        deflateEnd(&stream);
        return std::unexpected(make_status(status_code::internal, "gzip deflate failed"));
    }
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return out;
#else
    (void)payload;
    return std::unexpected(make_status(status_code::unimplemented, "gzip compression requires zlib"));
#endif
}

inline auto gzip_decompress(std::span<const std::byte> payload, std::size_t max_message_bytes)
    -> std::expected<byte_buffer, status>
{
#ifdef CNETMOD_HAS_ZLIB
    z_stream stream{};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
        return std::unexpected(make_status(status_code::internal, "gzip inflate init failed"));
    }

    byte_buffer out;
    out.resize(std::min<std::size_t>(std::max<std::size_t>(payload.size() * 3, 256), max_message_bytes));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(payload.data()));
    stream.avail_in = static_cast<uInt>(payload.size());

    int rc = Z_OK;
    while (rc == Z_OK) {
        if (stream.total_out == out.size()) {
            if (out.size() >= max_message_bytes) {
                inflateEnd(&stream);
                return std::unexpected(make_status(status_code::resource_exhausted, "grpc message exceeds receive limit"));
            }
            out.resize(std::min<std::size_t>(out.size() * 2, max_message_bytes));
        }
        stream.next_out = reinterpret_cast<Bytef*>(out.data() + stream.total_out);
        stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
        rc = inflate(&stream, Z_NO_FLUSH);
    }

    if (rc != Z_STREAM_END) {
        inflateEnd(&stream);
        return std::unexpected(make_status(status_code::invalid_argument, "invalid gzip grpc message"));
    }
    out.resize(stream.total_out);
    inflateEnd(&stream);
    return out;
#else
    (void)payload;
    (void)max_message_bytes;
    return std::unexpected(make_status(status_code::unimplemented, "gzip decompression requires zlib"));
#endif
}

} // namespace detail

export auto encode_frame(std::span<const std::byte> payload, bool compressed = false)
    -> std::expected<byte_buffer, std::error_code>
{
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(make_error_code(std::errc::message_size));
    }
    byte_buffer out;
    out.reserve(payload.size() + 5);
    detail::append_frame(out, payload, compressed);
    return out;
}

export auto encode_frames(std::span<const byte_buffer> messages, bool compressed = false)
    -> std::expected<byte_buffer, std::error_code>
{
    std::size_t total = 0;
    for (const auto& msg : messages) {
        if (msg.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(make_error_code(std::errc::message_size));
        }
        total += msg.size() + 5;
    }

    byte_buffer out;
    out.reserve(total);
    for (const auto& msg : messages) {
        detail::append_frame(out, msg, compressed);
    }
    return out;
}

export auto encode_frames(std::span<const byte_buffer> messages, compression_algorithm compression)
    -> std::expected<byte_buffer, status>
{
    byte_buffer out;
    if (compression == compression_algorithm::identity) {
        std::size_t total = 0;
        for (const auto& msg : messages) {
            if (msg.size() > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected(make_status(status_code::resource_exhausted, "grpc message exceeds frame limit"));
            }
            total += msg.size() + 5;
        }
        out.reserve(total);
        for (const auto& msg : messages) {
            detail::append_frame(out, msg, false);
        }
        return out;
    }

    for (const auto& msg : messages) {
        std::span<const std::byte> payload{msg.data(), msg.size()};
        byte_buffer compressed_payload;
        auto zipped = detail::gzip_compress(payload);
        if (!zipped) return std::unexpected(zipped.error());
        compressed_payload = std::move(*zipped);
        payload = compressed_payload;
        if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(make_status(status_code::resource_exhausted, "grpc message exceeds frame limit"));
        }
        detail::append_frame(out, payload, true);
    }
    return out;
}

export auto decode_frames(std::span<const std::byte> data)
    -> std::expected<std::vector<message_frame>, std::error_code>
{
    std::vector<message_frame> frames;
    std::size_t pos = 0;
    while (pos < data.size()) {
        if (data.size() - pos < 5) {
            return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        bool compressed = std::to_integer<unsigned char>(data[pos]) != 0;
        auto n = (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos + 1])) << 24) |
                 (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos + 2])) << 16) |
                 (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos + 3])) << 8) |
                 static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos + 4]));
        pos += 5;
        if (pos + n > data.size()) {
            return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        message_frame frame{.compressed = compressed};
        frame.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                             data.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        frames.push_back(std::move(frame));
    }
    return frames;
}

export class stream_decoder {
public:
    [[nodiscard]] auto feed(std::span<const std::byte> bytes)
        -> std::expected<std::vector<message_frame>, std::error_code>
    {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
        std::vector<message_frame> ready;

        while (buffer_.size() >= 5) {
            auto n = (static_cast<std::uint32_t>(std::to_integer<unsigned char>(buffer_[1])) << 24) |
                     (static_cast<std::uint32_t>(std::to_integer<unsigned char>(buffer_[2])) << 16) |
                     (static_cast<std::uint32_t>(std::to_integer<unsigned char>(buffer_[3])) << 8) |
                     static_cast<std::uint32_t>(std::to_integer<unsigned char>(buffer_[4]));
            if (buffer_.size() < static_cast<std::size_t>(n) + 5) break;
            auto one = decode_frames(std::span<const std::byte>{buffer_.data(), static_cast<std::size_t>(n) + 5});
            if (!one || one->empty()) {
                return std::unexpected(one ? make_error_code(std::errc::protocol_error) : one.error());
            }
            ready.push_back(std::move(one->front()));
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(n + 5));
        }
        return ready;
    }

    [[nodiscard]] auto buffered_bytes() const noexcept -> std::size_t {
        return buffer_.size();
    }

private:
    byte_buffer buffer_;
};

export auto frames_to_messages(std::span<const message_frame> frames, codec_options options)
    -> std::expected<std::vector<byte_buffer>, status>
{
    std::vector<byte_buffer> out;
    out.reserve(frames.size());
    for (const auto& frame : frames) {
        if (!frame.compressed && frame.payload.size() > options.max_message_bytes) {
            return std::unexpected(status{
                .code = status_code::resource_exhausted,
                .message = "grpc message exceeds receive limit",
            });
        }
        if (frame.compressed) {
            if (!options.accept_compressed) {
                return std::unexpected(status{
                    .code = status_code::unimplemented,
                    .message = "compressed grpc messages are not enabled",
                });
            }
            if (options.compression != compression_algorithm::gzip) {
                return std::unexpected(status{
                    .code = status_code::unimplemented,
                    .message = "unsupported grpc compression algorithm",
                });
            }
            auto inflated = detail::gzip_decompress(frame.payload, options.max_message_bytes);
            if (!inflated) return std::unexpected(inflated.error());
            out.push_back(std::move(*inflated));
            continue;
        }
        out.push_back(frame.payload);
    }
    return out;
}

export auto frames_to_messages(std::span<const message_frame> frames)
    -> std::expected<std::vector<byte_buffer>, status>
{
    return frames_to_messages(frames, codec_options{});
}

} // namespace cnetmod::grpc
