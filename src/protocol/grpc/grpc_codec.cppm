module;
#include <cnetmod/config.hpp>
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
export auto encode_frame(std::span<const std::byte> payload,
                         bool compressed = false)
    -> std::expected<byte_buffer, std::error_code>;
export auto encode_frames(std::span<const byte_buffer> messages,
                          bool compressed = false)
    -> std::expected<byte_buffer, std::error_code>;
export auto encode_frames(std::span<const byte_buffer> messages,
                          compression_algorithm compression)
    -> std::expected<byte_buffer, status>;
export auto decode_frames(std::span<const std::byte> data)
    -> std::expected<std::vector<message_frame>, std::error_code>;
export class stream_decoder {
public:
  [[nodiscard]] auto feed(std::span<const std::byte> bytes)
      -> std::expected<std::vector<message_frame>, std::error_code>;
  [[nodiscard]] auto buffered_bytes() const noexcept -> std::size_t;

private:
  byte_buffer buffer_;
};
export auto frames_to_messages(std::span<const message_frame> frames,
                               codec_options options)
    -> std::expected<std::vector<byte_buffer>, status>;
export auto frames_to_messages(std::span<const message_frame> frames)
    -> std::expected<std::vector<byte_buffer>, status>;
} // namespace cnetmod::grpc
