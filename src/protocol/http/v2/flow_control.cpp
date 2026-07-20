module cnetmod.protocol.http.v2.flow_control;

import std;

namespace cnetmod::http::v2 {
flow_window::flow_window(std::int32_t initial) noexcept : value_(initial) {}

auto flow_window::available() const noexcept -> std::int32_t { return value_; }

auto flow_window::consume(std::uint32_t bytes) noexcept -> bool {
  if (bytes > static_cast<std::uint32_t>(std::max(value_, 0)))
    return false;
  value_ -= static_cast<std::int32_t>(bytes);
  return true;
}

auto flow_window::increase(std::uint32_t bytes) noexcept -> bool {
  constexpr auto max_window = std::numeric_limits<std::int32_t>::max();
  if (bytes == 0 || bytes > static_cast<std::uint32_t>(max_window - value_))
    return false;
  value_ += static_cast<std::int32_t>(bytes);
  return true;
}
} // namespace cnetmod::http::v2