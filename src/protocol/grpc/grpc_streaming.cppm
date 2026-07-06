module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.streaming;

import std;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.codec;

namespace cnetmod::grpc {

export class message_stream_decoder {
public:
    explicit message_stream_decoder(codec_options options = {})
        : options_(options) {}

    [[nodiscard]] auto feed(std::span<const std::byte> bytes)
        -> std::expected<std::vector<byte_buffer>, status>
    {
        auto frames = decoder_.feed(bytes);
        if (!frames) {
            return std::unexpected(make_status(
                status_code::invalid_argument, frames.error().message()));
        }
        return frames_to_messages(*frames, options_);
    }

    [[nodiscard]] auto buffered_bytes() const noexcept -> std::size_t {
        return decoder_.buffered_bytes();
    }

private:
    stream_decoder decoder_;
    codec_options options_;
};

export class message_stream_encoder {
public:
    explicit message_stream_encoder(compression_algorithm compression = compression_algorithm::identity)
        : compression_(compression) {}

    [[nodiscard]] auto encode(std::span<const std::byte> message)
        -> std::expected<byte_buffer, status>
    {
        byte_buffer msg(message.begin(), message.end());
        std::array<byte_buffer, 1> one{std::move(msg)};
        return encode_frames(std::span<const byte_buffer>{one.data(), one.size()}, compression_);
    }

private:
    compression_algorithm compression_ = compression_algorithm::identity;
};

} // namespace cnetmod::grpc
