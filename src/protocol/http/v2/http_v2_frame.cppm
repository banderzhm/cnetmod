export module cnetmod.protocol.http.v2.frame;

import std;

export namespace cnetmod::http::v2 {
enum class frame_type : std::uint8_t {
  data = 0x0,
  headers = 0x1,
  priority = 0x2,
  rst_stream = 0x3,
  settings = 0x4,
  push_promise = 0x5,
  ping = 0x6,
  goaway = 0x7,
  window_update = 0x8,
  continuation = 0x9,
};

struct frame_header {
  std::uint32_t length{};
  frame_type type{};
  std::uint8_t flags{};
  std::uint32_t stream_id{};
};

inline constexpr std::size_t frame_header_size = 9;
inline constexpr std::uint32_t max_frame_payload = 16 * 1024 * 1024 - 1;

[[nodiscard]] auto decode_frame_header(std::span<const std::byte> input)
    -> std::expected<frame_header, std::error_code>;
[[nodiscard]] auto encode_frame_header(frame_header header)
    -> std::array<std::byte, frame_header_size>;
} // namespace cnetmod::http::v2
