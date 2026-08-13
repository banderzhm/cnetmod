module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http.v3.frame;

import std;
import cnetmod.core.buffer;
import cnetmod.utils.flat_map;

namespace cnetmod::http::v3 {
/// HTTP/3 frame types defined by RFC 9114 section 7.
export enum class http3_frame_type : std::uint64_t
{
    data = 0x00,
    headers = 0x01,
    cancel_push = 0x03,
    settings = 0x04,
    push_promise = 0x05,
    goaway = 0x07,
    max_push_id = 0x0d,
    /// RFC 9218, HTTP/3 Priority Update.
    priority_update = 0x0f0700,
};

export struct data_frame
{
    byte_view data;
};

export struct headers_frame
{
    byte_view encoded_headers;
};

export struct settings_frame
{
    cnetmod::flat_map<std::uint64_t,
        std::variant<std::uint64_t, std::string>>
        settings;
};

export struct push_promise_frame
{
    std::uint64_t promised_stream_id;
    byte_view encoded_headers;
};

export struct goaway_frame
{
    std::uint64_t stream_id;
    std::optional<std::uint64_t> error_code;
    std::string reason;
};

export struct max_push_id_frame
{
    std::uint64_t max_push_id;
};

export struct cancel_push_frame
{
    std::uint64_t push_id;
};

/// RFC 9218 urgency values: 0 is most urgent and 7 is least urgent.
/// `incremental` asks a sender to interleave this response with peers of the
/// same urgency instead of emitting it as one contiguous burst.
export struct http_priority
{
    std::uint8_t urgency{3};
    bool incremental{};
};

/// PRIORITY_UPDATE is sent on the control stream. The field value uses the
/// Structured Fields dictionary defined by RFC 9218 (for example `u=1, i`).
export struct priority_update_frame
{
    std::uint64_t prioritized_element_id{};
    std::string priority_field_value;
};

/// RFC 9297 payload carried inside one QUIC DATAGRAM: a QUIC variable-length
/// Context ID followed by application bytes. Context assignment belongs to
/// the HTTP request or WebTransport session that created it.
export struct http_datagram
{
    std::uint64_t context_id{};
    byte_view payload{};
};

/// An extension frame that this implementation does not interpret.  RFC 9114
/// requires endpoints to ignore unknown frame types after consuming the
/// complete payload; representing it explicitly avoids silently treating it
/// as a standard frame.
export struct unknown_frame
{
    std::uint64_t type;
    byte_view payload;
};

export using http3_frame_variant = std::variant<data_frame, headers_frame, settings_frame,
    push_promise_frame, goaway_frame, max_push_id_frame, cancel_push_frame,
    priority_update_frame, unknown_frame>;

export enum class http3_setting_key : std::uint64_t
{
    unknown = 0,
    qpack_max_table_capacity = 0x01,
    max_header_list_size = 0x06,
    qpack_blocked_streams = 0x07,
    enable_connect_protocol = 0x08,
    h3_datagram = 0x33,
    /// Chromium's WebTransport implementation still recognizes this
    /// pre-RFC HTTP Datagram setting in addition to SETTINGS_H3_DATAGRAM.
    /// Unknown SETTINGS are explicitly ignorable, so advertising it alongside
    /// the standardized value is safe for non-Chromium peers.
    h3_datagram_chrome_legacy = 0xffd277,
    /// draft-ietf-webtrans-http3 SETTINGS_ENABLE_WEBTRANSPORT.  Current
    /// browsers still require this setting before accepting Extended CONNECT
    /// with :protocol = webtransport.
    enable_webtransport = 0x2b603742,
    webtransport_max_sessions = 0xc671706a,
};

export [[nodiscard]] auto decode_http3_frame(byte_view data)
    -> std::expected<std::pair<http3_frame_variant, std::size_t>, std::error_code>;

export [[nodiscard]] auto encode_http3_frame(const data_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const headers_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const settings_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const push_promise_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const goaway_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const max_push_id_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const cancel_push_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const priority_update_frame& frame) -> byte_buffer;
export [[nodiscard]] auto encode_http3_frame(const http3_frame_variant& frame) -> byte_buffer;

/// Parse/format the standardized `u` and `i` priority members. Unknown
/// extension members are ignored so peers can evolve priority metadata.
export [[nodiscard]] auto parse_http_priority(std::string_view value)
    -> std::expected<http_priority, std::error_code>;
export [[nodiscard]] auto format_http_priority(http_priority priority) -> std::string;

/// Encode/decode the payload of an RFC 9297 HTTP Datagram. These helpers do
/// not add an HTTP/3 frame header: the result is passed directly to QUIC's
/// DATAGRAM transport primitive.
export [[nodiscard]] auto encode_http_datagram(const http_datagram& datagram) -> byte_buffer;
export [[nodiscard]] auto decode_http_datagram(byte_view bytes)
    -> std::expected<http_datagram, std::error_code>;

export [[nodiscard]] auto is_stream_frame(const http3_frame_variant& frame) noexcept -> bool;
} // namespace cnetmod::http::v3
