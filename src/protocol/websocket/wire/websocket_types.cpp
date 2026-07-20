module cnetmod.protocol.websocket; // implementation unit
import :types;

namespace cnetmod::ws {
namespace {
class ws_error_category final : public std::error_category {
public:
  auto name() const noexcept -> const char * override { return "websocket"; }

  auto message(int value) const -> std::string override {
    switch (static_cast<ws_errc>(value)) {
    case ws_errc::success:
      return "success";
    case ws_errc::need_more_data:
      return "need more data";
    case ws_errc::invalid_frame:
      return "invalid frame";
    case ws_errc::reserved_opcode:
      return "reserved opcode";
    case ws_errc::control_frame_too_large:
      return "control frame too large";
    case ws_errc::fragmented_control:
      return "fragmented control frame";
    case ws_errc::masked_from_server:
      return "masked frame from server";
    case ws_errc::unmasked_from_client:
      return "unmasked frame from client";
    case ws_errc::invalid_close_payload:
      return "invalid close payload";
    case ws_errc::invalid_utf8:
      return "invalid UTF-8";
    case ws_errc::handshake_failed:
      return "handshake failed";
    case ws_errc::not_connected:
      return "not connected";
    case ws_errc::already_closed:
      return "already closed";
    case ws_errc::protocol_error:
      return "protocol error";
    }
    return "unknown websocket error";
  }
};

auto category() -> const std::error_category & {
  static const ws_error_category instance;
  return instance;
}
} // namespace

auto is_control(opcode op) noexcept -> bool {
  return static_cast<std::uint8_t>(op) >= 0x8;
}

auto opcode_to_string(opcode op) noexcept -> std::string_view {
  switch (op) {
  case opcode::continuation:
    return "continuation";
  case opcode::text:
    return "text";
  case opcode::binary:
    return "binary";
  case opcode::close:
    return "close";
  case opcode::ping:
    return "ping";
  case opcode::pong:
    return "pong";
  }
  return "unknown";
}

auto close_reason_string(std::uint16_t code) noexcept -> std::string_view {
  switch (code) {
  case 1000:
    return "Normal Closure";
  case 1001:
    return "Going Away";
  case 1002:
    return "Protocol Error";
  case 1003:
    return "Unsupported Data";
  case 1005:
    return "No Status Received";
  case 1006:
    return "Abnormal Closure";
  case 1007:
    return "Invalid Payload Data";
  case 1008:
    return "Policy Violation";
  case 1009:
    return "Message Too Big";
  case 1010:
    return "Extension Required";
  case 1011:
    return "Internal Error";
  case 1015:
    return "TLS Handshake Failure";
  }
  return "Unknown";
}

auto ws_message::as_string() const noexcept -> std::string_view {
  return {reinterpret_cast<const char *>(payload.data()), payload.size()};
}

auto make_error_code(ws_errc value) noexcept -> std::error_code {
  return {static_cast<int>(value), category()};
}
} // namespace cnetmod::ws
