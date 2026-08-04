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
    push_promise_frame, goaway_frame, max_push_id_frame, cancel_push_frame, unknown_frame>;

export enum class http3_setting_key : std::uint64_t
{
    unknown = 0,
    qpack_max_table_capacity = 0x01,
    max_header_list_size = 0x06,
    qpack_blocked_streams = 0x07,
    enable_connect_protocol = 0x08,
    h3_datagram = 0x33,
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
export [[nodiscard]] auto encode_http3_frame(const http3_frame_variant& frame) -> byte_buffer;

export [[nodiscard]] auto is_stream_frame(const http3_frame_variant& frame) noexcept -> bool;
} // namespace cnetmod::http::v3
