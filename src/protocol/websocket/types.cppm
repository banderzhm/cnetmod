module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:types;

import std;

namespace cnetmod::ws {

// =============================================================================
// WebSocket Opcode (RFC 6455 §5.2)
// =============================================================================

export enum class opcode : std::uint8_t {
    continuation = 0x0,
    text         = 0x1,
    binary       = 0x2,
    // 0x3-0x7 reserved for non-control
    close        = 0x8,
    ping         = 0x9,
    pong         = 0xA,
    // 0xB-0xF reserved for control
};

export constexpr auto is_control(opcode op) noexcept -> bool {
    return static_cast<std::uint8_t>(op) >= 0x8;
}

export constexpr auto opcode_to_string(opcode op) noexcept -> std::string_view {
    switch (op) {
        case opcode::continuation: return "continuation";
        case opcode::text:         return "text";
        case opcode::binary:       return "binary";
        case opcode::close:        return "close";
        case opcode::ping:         return "ping";
        case opcode::pong:         return "pong";
        default:                   return "unknown";
    }
}

// =============================================================================
// WebSocket Close Codes (RFC 6455 §7.4.1)
// =============================================================================

export namespace close_code {
    inline constexpr std::uint16_t normal              = 1000;
    inline constexpr std::uint16_t going_away          = 1001;
    inline constexpr std::uint16_t protocol_error      = 1002;
    inline constexpr std::uint16_t unsupported_data    = 1003;
    inline constexpr std::uint16_t no_status           = 1005;  // Cannot be sent
    inline constexpr std::uint16_t abnormal_close      = 1006;  // Cannot be sent
    inline constexpr std::uint16_t invalid_payload     = 1007;
    inline constexpr std::uint16_t policy_violation    = 1008;
    inline constexpr std::uint16_t message_too_big     = 1009;
    inline constexpr std::uint16_t extension_required  = 1010;
    inline constexpr std::uint16_t internal_error      = 1011;
    inline constexpr std::uint16_t tls_handshake       = 1015;  // Cannot be sent
}

export constexpr auto close_reason_string(std::uint16_t code) noexcept -> std::string_view {
    switch (code) {
        case 1000: return "Normal Closure";
        case 1001: return "Going Away";
        case 1002: return "Protocol Error";
        case 1003: return "Unsupported Data";
        case 1005: return "No Status Received";
        case 1006: return "Abnormal Closure";
        case 1007: return "Invalid Payload Data";
        case 1008: return "Policy Violation";
        case 1009: return "Message Too Big";
        case 1010: return "Extension Required";
        case 1011: return "Internal Error";
        case 1015: return "TLS Handshake Failure";
        default:   return "Unknown";
    }
}

// =============================================================================
// Frame Header
// =============================================================================

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

// =============================================================================
// WebSocket Message
// =============================================================================

export struct ws_message {
    opcode op = opcode::text;
    std::vector<std::byte> payload;

    /// Access payload as string_view
    [[nodiscard]] auto as_string() const noexcept -> std::string_view {
        return {reinterpret_cast<const char*>(payload.data()), payload.size()};
    }
};

// =============================================================================
// WebSocket Error Codes
// =============================================================================

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
    protocol_error,
};

namespace detail {

class ws_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override { return "websocket"; }
    auto message(int ev) const -> std::string override {
        switch (static_cast<ws_errc>(ev)) {
            case ws_errc::success:                   return "success";
            case ws_errc::need_more_data:            return "need more data";
            case ws_errc::invalid_frame:             return "invalid frame";
            case ws_errc::reserved_opcode:           return "reserved opcode";
            case ws_errc::control_frame_too_large:   return "control frame too large";
            case ws_errc::fragmented_control:        return "fragmented control frame";
            case ws_errc::masked_from_server:        return "masked frame from server";
            case ws_errc::unmasked_from_client:      return "unmasked frame from client";
            case ws_errc::invalid_close_payload:     return "invalid close payload";
            case ws_errc::invalid_utf8:              return "invalid UTF-8";
            case ws_errc::handshake_failed:          return "handshake failed";
            case ws_errc::not_connected:             return "not connected";
            case ws_errc::already_closed:            return "already closed";
            case ws_errc::protocol_error:            return "protocol error";
            default:                                 return "unknown websocket error";
        }
    }
};

inline auto ws_category_instance() -> const std::error_category& {
    static const ws_error_category_impl instance;
    return instance;
}

} // namespace detail

export inline auto make_error_code(ws_errc e) noexcept -> std::error_code {
    return {static_cast<int>(e), detail::ws_category_instance()};
}

} // namespace cnetmod::ws

template <>
struct std::is_error_code_enum<cnetmod::ws::ws_errc> : std::true_type {};
