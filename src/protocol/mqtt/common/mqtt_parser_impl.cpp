module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import cnetmod.core.log;
import :parser;

namespace cnetmod::mqtt {

mqtt_parser::mqtt_parser() = default;

void mqtt_parser::feed(std::string_view data) { buf_.append(data); }

auto mqtt_parser::next() -> std::optional<mqtt_frame> {
  if (buf_.size() - pos_ < 2)
    return std::nullopt;

  const auto first_byte = static_cast<std::uint8_t>(buf_[pos_]);
  const auto packet_type = get_packet_type(first_byte);
  const auto flags = get_packet_flags(first_byte);
  const auto [remaining_length, variable_length_bytes] =
      detail::decode_variable_length(std::string_view(buf_).substr(pos_ + 1));

  if (variable_length_bytes == 0) {
    const auto available_length = buf_.size() - pos_ - 1;
    if (available_length < 4)
      return std::nullopt;

    logger::debug("mqtt parser: invalid remaining length, skipping byte");
    ++pos_;
    return std::nullopt;
  }

  const std::size_t total_length = 1 + variable_length_bytes + remaining_length;
  if (buf_.size() - pos_ < total_length)
    return std::nullopt;

  mqtt_frame frame;
  frame.type = packet_type;
  frame.flags = flags;
  frame.payload =
      buf_.substr(pos_ + 1 + variable_length_bytes, remaining_length);
  pos_ += total_length;
  compact();
  return frame;
}

void mqtt_parser::reset() {
  buf_.clear();
  pos_ = 0;
}

auto mqtt_parser::pending() const noexcept -> std::size_t {
  return buf_.size() - pos_;
}

void mqtt_parser::compact() {
  if (pos_ == buf_.size()) {
    buf_.clear();
    pos_ = 0;
  } else if (pos_ > 64 * 1024 && pos_ * 2 > buf_.size()) {
    buf_.erase(0, pos_);
    pos_ = 0;
  }
}

} // namespace cnetmod::mqtt
