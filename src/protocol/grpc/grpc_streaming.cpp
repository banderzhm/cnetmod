module cnetmod.protocol.grpc.streaming;

import std;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.codec;

namespace cnetmod::grpc {

message_stream_decoder::message_stream_decoder(codec_options options)
    : options_(options) {}
auto message_stream_decoder::feed(std::span<const std::byte> bytes)
    -> std::expected<std::vector<byte_buffer>, status> {
  auto frames = decoder_.feed(bytes);
  if (!frames)
    return std::unexpected(
        make_status(status_code::invalid_argument, frames.error().message()));
  return frames_to_messages(*frames, options_);
}
auto message_stream_decoder::buffered_bytes() const noexcept -> std::size_t {
  return decoder_.buffered_bytes();
}
message_stream_encoder::message_stream_encoder(
    compression_algorithm compression)
    : compression_(compression) {}
auto message_stream_encoder::encode(std::span<const std::byte> message)
    -> std::expected<byte_buffer, status> {
  byte_buffer copy(message.begin(), message.end());
  std::array<byte_buffer, 1> one{std::move(copy)};
  return encode_frames(std::span<const byte_buffer>{one.data(), one.size()},
                       compression_);
}

} // namespace cnetmod::grpc
