module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
#include <zlib.h>
#endif
module cnetmod.protocol.grpc.codec;
import std;
import cnetmod.core.error;
import cnetmod.protocol.grpc.types;
namespace cnetmod::grpc {
namespace {
void append_frame(byte_buffer &out, std::span<const std::byte> payload,
                  bool compressed) {
  const auto n = static_cast<std::uint32_t>(payload.size());
  out.push_back(compressed ? std::byte{1} : std::byte{0});
  out.push_back(static_cast<std::byte>(n >> 24));
  out.push_back(static_cast<std::byte>(n >> 16));
  out.push_back(static_cast<std::byte>(n >> 8));
  out.push_back(static_cast<std::byte>(n));
  out.insert(out.end(), payload.begin(), payload.end());
}
auto compress(std::span<const std::byte> payload)
    -> std::expected<byte_buffer, status> {
#ifdef CNETMOD_HAS_ZLIB
  z_stream stream{};
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16,
                   8, Z_DEFAULT_STRATEGY) != Z_OK)
    return std::unexpected(
        make_status(status_code::internal, "gzip deflate init failed"));
  byte_buffer out(std::max<std::size_t>(
      128, compressBound(static_cast<uLong>(payload.size()))));
  stream.next_in =
      reinterpret_cast<Bytef *>(const_cast<std::byte *>(payload.data()));
  stream.avail_in = static_cast<uInt>(payload.size());
  int result{};
  do {
    if (stream.total_out == out.size())
      out.resize(out.size() * 2);
    stream.next_out = reinterpret_cast<Bytef *>(out.data() + stream.total_out);
    stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
    result = deflate(&stream, Z_FINISH);
  } while (result == Z_OK);
  if (result != Z_STREAM_END) {
    deflateEnd(&stream);
    return std::unexpected(
        make_status(status_code::internal, "gzip deflate failed"));
  }
  out.resize(stream.total_out);
  deflateEnd(&stream);
  return out;
#else
  (void)payload;
  return std::unexpected(make_status(status_code::unimplemented,
                                     "gzip compression requires zlib"));
#endif
}
auto decompress(std::span<const std::byte> payload, std::size_t limit)
    -> std::expected<byte_buffer, status> {
#ifdef CNETMOD_HAS_ZLIB
  z_stream stream{};
  if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
    return std::unexpected(
        make_status(status_code::internal, "gzip inflate init failed"));
  byte_buffer out(std::min<std::size_t>(
      std::max<std::size_t>(payload.size() * 3, 256), limit));
  stream.next_in =
      reinterpret_cast<Bytef *>(const_cast<std::byte *>(payload.data()));
  stream.avail_in = static_cast<uInt>(payload.size());
  int result = Z_OK;
  while (result == Z_OK) {
    if (stream.total_out == out.size()) {
      if (out.size() >= limit) {
        inflateEnd(&stream);
        return std::unexpected(
            make_status(status_code::resource_exhausted,
                        "grpc message exceeds receive limit"));
      }
      out.resize(std::min(out.size() * 2, limit));
    }
    stream.next_out = reinterpret_cast<Bytef *>(out.data() + stream.total_out);
    stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
    result = inflate(&stream, Z_NO_FLUSH);
  }
  if (result != Z_STREAM_END) {
    inflateEnd(&stream);
    return std::unexpected(make_status(status_code::invalid_argument,
                                       "invalid gzip grpc message"));
  }
  out.resize(stream.total_out);
  inflateEnd(&stream);
  return out;
#else
  (void)payload;
  (void)limit;
  return std::unexpected(make_status(status_code::unimplemented,
                                     "gzip decompression requires zlib"));
#endif
}
auto frame_length(std::span<const std::byte> data) -> std::uint32_t {
  return static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[1]))
             << 24 |
         static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[2]))
             << 16 |
         static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[3]))
             << 8 |
         std::to_integer<unsigned char>(data[4]);
}
} // namespace
auto encode_frame(std::span<const std::byte> payload, bool compressed)
    -> std::expected<byte_buffer, std::error_code> {
  if (payload.size() > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(make_error_code(std::errc::message_size));
  byte_buffer out;
  out.reserve(payload.size() + 5);
  append_frame(out, payload, compressed);
  return out;
}
auto encode_frames(std::span<const byte_buffer> messages, bool compressed)
    -> std::expected<byte_buffer, std::error_code> {
  std::size_t total{};
  for (const auto &message : messages) {
    if (message.size() > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected(make_error_code(std::errc::message_size));
    total += message.size() + 5;
  }
  byte_buffer out;
  out.reserve(total);
  for (const auto &message : messages)
    append_frame(out, message, compressed);
  return out;
}
auto encode_frames(std::span<const byte_buffer> messages,
                   compression_algorithm algorithm)
    -> std::expected<byte_buffer, status> {
  byte_buffer out;
  for (const auto &message : messages) {
    if (message.size() > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected(make_status(status_code::resource_exhausted,
                                         "grpc message exceeds frame limit"));
    if (algorithm == compression_algorithm::identity)
      append_frame(out, message, false);
    else {
      auto payload = compress(message);
      if (!payload)
        return std::unexpected(payload.error());
      append_frame(out, *payload, true);
    }
  }
  return out;
}
auto decode_frames(std::span<const std::byte> data)
    -> std::expected<std::vector<message_frame>, std::error_code> {
  std::vector<message_frame> out;
  for (std::size_t pos{}; pos < data.size();) {
    if (data.size() - pos < 5)
      return std::unexpected(make_error_code(std::errc::protocol_error));
    const auto n = frame_length(data.subspan(pos));
    if (n > data.size() - pos - 5)
      return std::unexpected(make_error_code(std::errc::protocol_error));
    message_frame frame{.compressed =
                            std::to_integer<unsigned char>(data[pos]) != 0};
    frame.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(pos + 5),
                         data.begin() +
                             static_cast<std::ptrdiff_t>(pos + 5 + n));
    out.push_back(std::move(frame));
    pos += 5 + n;
  }
  return out;
}
auto stream_decoder::feed(std::span<const std::byte> bytes)
    -> std::expected<std::vector<message_frame>, std::error_code> {
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  std::vector<message_frame> ready;
  while (buffer_.size() >= 5) {
    const auto n = frame_length(buffer_);
    if (buffer_.size() < n + 5)
      break;
    auto one = decode_frames({buffer_.data(), static_cast<std::size_t>(n + 5)});
    if (!one || one->empty())
      return std::unexpected(one ? make_error_code(std::errc::protocol_error)
                                 : one.error());
    ready.push_back(std::move(one->front()));
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(n + 5));
  }
  return ready;
}
auto stream_decoder::buffered_bytes() const noexcept -> std::size_t {
  return buffer_.size();
}
auto frames_to_messages(std::span<const message_frame> frames,
                        codec_options options)
    -> std::expected<std::vector<byte_buffer>, status> {
  std::vector<byte_buffer> out;
  out.reserve(frames.size());
  for (const auto &frame : frames) {
    if (!frame.compressed) {
      if (frame.payload.size() > options.max_message_bytes)
        return std::unexpected(
            make_status(status_code::resource_exhausted,
                        "grpc message exceeds receive limit"));
      out.push_back(frame.payload);
      continue;
    }
    if (!options.accept_compressed)
      return std::unexpected(
          make_status(status_code::unimplemented,
                      "compressed grpc messages are not enabled"));
    if (options.compression != compression_algorithm::gzip)
      return std::unexpected(
          make_status(status_code::unimplemented,
                      "unsupported grpc compression algorithm"));
    auto payload = decompress(frame.payload, options.max_message_bytes);
    if (!payload)
      return std::unexpected(payload.error());
    out.push_back(std::move(*payload));
  }
  return out;
}
auto frames_to_messages(std::span<const message_frame> frames)
    -> std::expected<std::vector<byte_buffer>, status> {
  return frames_to_messages(frames, {});
}
} // namespace cnetmod::grpc
