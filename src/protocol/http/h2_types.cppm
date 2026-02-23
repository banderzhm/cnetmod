module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_NGHTTP2
#define NGHTTP2_NO_SSIZE_T
#include <nghttp2/nghttp2.h>
#endif

export module cnetmod.protocol.http:h2_types;

import std;

namespace cnetmod::http {

#ifdef CNETMOD_HAS_NGHTTP2

// =============================================================================
// HTTP/2 Error Codes (RFC 9113 Section 7)
// =============================================================================

export enum class h2_errc {
    success              = 0,
    no_error             = 0,
    protocol_error       = 1,
    internal_error       = 2,
    flow_control_error   = 3,
    settings_timeout     = 4,
    stream_closed        = 5,
    frame_size_error     = 6,
    refused_stream       = 7,
    cancel               = 8,
    compression_error    = 9,
    connect_error        = 10,
    enhance_your_calm    = 11,
    inadequate_security  = 12,
    http_1_1_required    = 13,
};

namespace detail {

class h2_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override { return "http2"; }
    auto message(int ev) const -> std::string override {
        switch (static_cast<h2_errc>(ev)) {
            case h2_errc::no_error:            return "no error";
            case h2_errc::protocol_error:      return "protocol error";
            case h2_errc::internal_error:       return "internal error";
            case h2_errc::flow_control_error:   return "flow control error";
            case h2_errc::settings_timeout:     return "settings timeout";
            case h2_errc::stream_closed:        return "stream closed";
            case h2_errc::frame_size_error:     return "frame size error";
            case h2_errc::refused_stream:       return "refused stream";
            case h2_errc::cancel:               return "cancel";
            case h2_errc::compression_error:    return "compression error";
            case h2_errc::connect_error:        return "connect error";
            case h2_errc::enhance_your_calm:    return "enhance your calm";
            case h2_errc::inadequate_security:  return "inadequate security";
            case h2_errc::http_1_1_required:    return "HTTP/1.1 required";
            default:                            return "unknown h2 error";
        }
    }
};

inline auto h2_category_instance() -> const std::error_category& {
    static const h2_error_category_impl instance;
    return instance;
}

} // namespace detail

export inline auto make_error_code(h2_errc e) noexcept -> std::error_code {
    return {static_cast<int>(e), detail::h2_category_instance()};
}

// =============================================================================
// HTTP/2 Stream State
// =============================================================================

export enum class h2_stream_state {
    idle,
    open,
    half_closed_local,
    half_closed_remote,
    closed,
};

// =============================================================================
// HTTP/2 Server Settings
// =============================================================================

export struct h2_settings {
    std::uint32_t max_concurrent_streams = 100;
    std::uint32_t initial_window_size    = 1 * 1024 * 1024;  // 1MB (RFC default 64KB is too small)
    std::uint32_t connection_window_size = 4 * 1024 * 1024;  // 4MB connection-level window
    std::uint32_t max_frame_size         = 16384;
    std::uint32_t max_header_list_size   = 65536;
    bool          enable_push            = false;  // Server push (typically disabled)
};

// =============================================================================
// HTTP/2 Client Connection Preface Magic
// =============================================================================

/// The 24-byte client connection preface string "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
export inline constexpr std::string_view h2_client_magic =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

/// Check if buffer starts with the HTTP/2 client connection preface
export constexpr auto is_h2_client_preface(const void* data, std::size_t len)
    noexcept -> bool
{
    if (len < h2_client_magic.size()) return false;
    auto* p = static_cast<const char*>(data);
    return std::string_view(p, h2_client_magic.size()) == h2_client_magic;
}

#endif // CNETMOD_HAS_NGHTTP2

} // namespace cnetmod::http

#ifdef CNETMOD_HAS_NGHTTP2
template <>
struct std::is_error_code_enum<cnetmod::http::h2_errc> : std::true_type {};
#endif
