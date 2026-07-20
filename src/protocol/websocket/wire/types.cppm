module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.websocket:types;
import std; // protocol type declarations

namespace cnetmod::ws {
export enum class opcode : std::uint8_t {
  continuation = 0x0,
  text = 0x1,
  binary = 0x2,
  close = 0x8,
  ping = 0x9,
  pong = 0xA
};

export auto is_control(opcode op) noexcept -> bool;
export auto opcode_to_string(opcode op) noexcept -> std::string_view;
export namespace close_code {
inline constexpr std::uint16_t normal = 1000, going_away = 1001,
                               protocol_error = 1002, unsupported_data = 1003;
inline constexpr std::uint16_t no_status = 1005, abnormal_close = 1006,
                               invalid_payload = 1007, policy_violation = 1008;
inline constexpr std::uint16_t message_too_big = 1009,
                               extension_required = 1010, internal_error = 1011,
                               tls_handshake = 1015;
} // namespace close_code
export auto close_reason_string(std::uint16_t code) noexcept
    -> std::string_view;

export struct frame_header {
  bool fin = true;
  bool rsv1 = false;
  bool rsv2 = false;
  bool rsv3 = false;
  opcode op = opcode::text;
  bool masked = false;
  std::uint64_t payload_length = 0;
  std::uint32_t masking_key = 0;
};

export struct ws_message {
  opcode op = opcode::text;
  std::vector<std::byte> payload;
  [[nodiscard]] auto as_string() const noexcept -> std::string_view;
};

export enum class ws_errc {
  success = 0,
  need_more_data,
  invalid_frame,
  reserved_opcode,
  control_frame_too_large,
  fragmented_control,
  masked_from_server,
  unmasked_from_client,
  invalid_close_payload,
  invalid_utf8,
  handshake_failed,
  not_connected,
  already_closed,
  protocol_error
};

export auto make_error_code(ws_errc e) noexcept -> std::error_code;
} // namespace cnetmod::ws

template <>
struct std::is_error_code_enum<cnetmod::ws::ws_errc> : std::true_type {};
