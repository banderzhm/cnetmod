module cnetmod.protocol.http.v2.stream;

import std;

namespace cnetmod::http::v2 {
stream::stream(std::uint32_t id, std::int32_t initial_window) noexcept
    : id_(id), receive_window_(initial_window), send_window_(initial_window) {}

auto stream::id() const noexcept -> std::uint32_t { return id_; }
auto stream::state() const noexcept -> stream_state { return state_; }
auto stream::headers() const noexcept -> const std::vector<header_field> & {
  return headers_;
}
auto stream::body() const noexcept -> std::span<const std::byte> {
  return body_;
}
auto stream::receive_window() noexcept -> flow_window & {
  return receive_window_;
}
auto stream::send_window() noexcept -> flow_window & { return send_window_; }

auto stream::receive_headers(std::vector<header_field> fields, bool end_stream)
    -> bool {
  if (state_ != stream_state::idle && state_ != stream_state::open)
    return false;
  headers_ = std::move(fields);
  state_ = end_stream ? stream_state::half_closed_remote : stream_state::open;
  return true;
}

auto stream::receive_data(std::span<const std::byte> data, bool end_stream)
    -> bool {
  if (state_ != stream_state::open ||
      !receive_window_.consume(static_cast<std::uint32_t>(data.size())))
    return false;
  body_.insert(body_.end(), data.begin(), data.end());
  if (end_stream)
    state_ = stream_state::half_closed_remote;
  return true;
}
} // namespace cnetmod::http::v2