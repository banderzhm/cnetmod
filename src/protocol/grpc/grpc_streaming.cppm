module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.streaming;

import std;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.codec;

namespace cnetmod::grpc {

export class message_stream_decoder {
public:
  explicit message_stream_decoder(codec_options options = {});
  [[nodiscard]] auto feed(std::span<const std::byte> bytes)
      -> std::expected<std::vector<byte_buffer>, status>;
  [[nodiscard]] auto buffered_bytes() const noexcept -> std::size_t;

private:
  stream_decoder decoder_;
  codec_options options_;
};

export class message_stream_encoder {
public:
  explicit message_stream_encoder(
      compression_algorithm compression = compression_algorithm::identity);
  [[nodiscard]] auto encode(std::span<const std::byte> message)
      -> std::expected<byte_buffer, status>;

private:
  compression_algorithm compression_ = compression_algorithm::identity;
};

} // namespace cnetmod::grpc
