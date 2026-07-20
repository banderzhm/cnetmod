module cnetmod.protocol.http.v2.settings;

import std;

namespace cnetmod::http::v2 {
auto decode_settings(std::span<const std::byte> payload, settings &target)
    -> std::error_code {
  if (payload.size() % 6 != 0)
    return std::make_error_code(std::errc::protocol_error);
  for (std::size_t offset = 0; offset < payload.size(); offset += 6) {
    const auto id = static_cast<setting_id>(
        (std::to_integer<std::uint16_t>(payload[offset]) << 8) |
        std::to_integer<std::uint16_t>(payload[offset + 1]));
    const auto value =
        (std::to_integer<std::uint32_t>(payload[offset + 2]) << 24) |
        (std::to_integer<std::uint32_t>(payload[offset + 3]) << 16) |
        (std::to_integer<std::uint32_t>(payload[offset + 4]) << 8) |
        std::to_integer<std::uint32_t>(payload[offset + 5]);
    switch (id) {
    case setting_id::header_table_size:
      target.header_table_size = value;
      break;
    case setting_id::enable_push:
      if (value > 1)
        return std::make_error_code(std::errc::protocol_error);
      target.enable_push = value != 0;
      break;
    case setting_id::max_concurrent_streams:
      target.max_concurrent_streams = value;
      break;
    case setting_id::initial_window_size:
      if (value > 0x7fff'ffffU)
        return std::make_error_code(std::errc::protocol_error);
      target.initial_window_size = value;
      break;
    case setting_id::max_frame_size:
      if (value < 16'384 || value > 16'777'215)
        return std::make_error_code(std::errc::protocol_error);
      target.max_frame_size = value;
      break;
    case setting_id::max_header_list_size:
      target.max_header_list_size = value;
      break;
    default:
      break;
    }
  }
  return {};
}

auto encode_settings(const settings &value) -> std::vector<std::byte> {
  std::vector<std::byte> output;
  const auto append = [&output](setting_id id, std::uint32_t item) {
    output.push_back(
        static_cast<std::byte>(static_cast<std::uint16_t>(id) >> 8));
    output.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(id)));
    output.push_back(static_cast<std::byte>(item >> 24));
    output.push_back(static_cast<std::byte>(item >> 16));
    output.push_back(static_cast<std::byte>(item >> 8));
    output.push_back(static_cast<std::byte>(item));
  };
  append(setting_id::header_table_size, value.header_table_size);
  append(setting_id::enable_push, value.enable_push ? 1U : 0U);
  append(setting_id::max_concurrent_streams, value.max_concurrent_streams);
  append(setting_id::initial_window_size, value.initial_window_size);
  append(setting_id::max_frame_size, value.max_frame_size);
  append(setting_id::max_header_list_size, value.max_header_list_size);
  return output;
}
} // namespace cnetmod::http::v2