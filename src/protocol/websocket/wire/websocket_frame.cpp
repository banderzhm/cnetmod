module;

#include <cstring>

module cnetmod.protocol.websocket; // implementation unit

import :frame;

namespace cnetmod::ws {
namespace {
constexpr std::uint8_t BHB0_FIN = 0x80, BHB0_RSV1 = 0x40, BHB0_RSV2 = 0x20,
                       BHB0_RSV3 = 0x10;
constexpr std::uint8_t BHB0_OPCODE = 0x0F, BHB1_MASK = 0x80,
                       BHB1_PAYLOAD = 0x7F;
} // namespace

auto parse_frame_header(std::span<const std::byte> data)
    -> std::expected<std::pair<frame_header, std::size_t>, std::error_code> {
  if (data.size() < 2)
    return std::unexpected(make_error_code(ws_errc::need_more_data));
  frame_header hdr;
  const auto b0 = static_cast<std::uint8_t>(data[0]),
             b1 = static_cast<std::uint8_t>(data[1]);
  hdr.fin = (b0 & BHB0_FIN) != 0;
  hdr.rsv1 = (b0 & BHB0_RSV1) != 0;
  hdr.rsv2 = (b0 & BHB0_RSV2) != 0;
  hdr.rsv3 = (b0 & BHB0_RSV3) != 0;
  hdr.op = static_cast<opcode>(b0 & BHB0_OPCODE);
  hdr.masked = (b1 & BHB1_MASK) != 0;
  const auto size = static_cast<std::uint8_t>(b1 & BHB1_PAYLOAD);
  std::size_t header_size = 2;
  if (size <= 125)
    hdr.payload_length = size;
  else if (size == 126) {
    if (data.size() < 4)
      return std::unexpected(make_error_code(ws_errc::need_more_data));
    hdr.payload_length =
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[2])) << 8) |
        static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[3]));
    header_size = 4;
  } else {
    if (data.size() < 10)
      return std::unexpected(make_error_code(ws_errc::need_more_data));
    for (auto i = 0; i < 8; ++i)
      hdr.payload_length =
          (hdr.payload_length << 8) | static_cast<std::uint8_t>(data[2 + i]);
    header_size = 10;
  }
  if (hdr.masked) {
    if (data.size() < header_size + 4)
      return std::unexpected(make_error_code(ws_errc::need_more_data));
    std::memcpy(&hdr.masking_key, data.data() + header_size, 4);
    header_size += 4;
  }
  if (is_control(hdr.op) && (hdr.payload_length > 125 || !hdr.fin))
    return std::unexpected(make_error_code(
        hdr.payload_length > 125 ? ws_errc::control_frame_too_large
                                 : ws_errc::fragmented_control));
  return std::pair{hdr, header_size};
}

void apply_mask(std::span<std::byte> data, std::uint32_t key) noexcept {
  const auto mask = reinterpret_cast<const std::byte *>(&key);
  for (std::size_t i = 0; i < data.size(); ++i)
    data[i] ^= mask[i & 3];
}

void build_frame_into(std::vector<std::byte> &frame, opcode op,
                      std::span<const std::byte> payload, bool mask, bool fin) {
  std::size_t header_size = 2;
  std::uint8_t basic_size;
  if (payload.size() <= 125)
    basic_size = static_cast<std::uint8_t>(payload.size());
  else if (payload.size() <= 0xffff) {
    basic_size = 126;
    header_size += 2;
  } else {
    basic_size = 127;
    header_size += 8;
  }
  if (mask)
    header_size += 4;
  frame.assign(header_size + payload.size(), std::byte{});
  frame[0] = static_cast<std::byte>(static_cast<std::uint8_t>(op) |
                                    (fin ? BHB0_FIN : 0));
  frame[1] = static_cast<std::byte>(basic_size | (mask ? BHB1_MASK : 0));
  std::size_t offset = 2;
  if (basic_size == 126) {
    const auto n = static_cast<std::uint16_t>(payload.size());
    frame[offset++] = static_cast<std::byte>(n >> 8);
    frame[offset++] = static_cast<std::byte>(n);
  } else if (basic_size == 127) {
    const auto n = static_cast<std::uint64_t>(payload.size());
    for (int i = 7; i >= 0; --i)
      frame[offset++] = static_cast<std::byte>(n >> (i * 8));
  }
  if (mask) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    const auto key = rng();
    std::memcpy(frame.data() + offset, &key, 4);
    offset += 4;
    std::memcpy(frame.data() + offset, payload.data(), payload.size());
    apply_mask({frame.data() + offset, payload.size()}, key);
  } else
    std::memcpy(frame.data() + offset, payload.data(), payload.size());
}

auto build_frame(opcode op, std::span<const std::byte> payload, bool mask,
                 bool fin) -> std::vector<std::byte> {
  std::vector<std::byte> frame;
  build_frame_into(frame, op, payload, mask, fin);
  return frame;
}

auto build_close_frame(std::uint16_t code, std::string_view reason, bool mask)
    -> std::vector<std::byte> {
  std::vector<std::byte> payload;
  payload.reserve(2 + reason.size());
  payload.push_back(static_cast<std::byte>(code >> 8));
  payload.push_back(static_cast<std::byte>(code));
  for (const auto c : reason)
    payload.push_back(static_cast<std::byte>(c));
  return build_frame(opcode::close, payload, mask);
}
} // namespace cnetmod::ws
