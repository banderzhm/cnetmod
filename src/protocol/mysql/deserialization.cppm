module;

#include <cnetmod/config.hpp>
#include <cstring>
#include <cstdio>

#ifdef _MSC_VER
#pragma warning(disable: 4996) // sscanf
#endif

export module cnetmod.protocol.mysql:deserialization;

import std;
import :types;
import :protocol;

namespace cnetmod::mysql::detail {

// =============================================================================
// server_greeting (Handshake V10)
// =============================================================================

struct server_greeting {
    std::uint8_t  protocol_version = 0;
    std::string   server_version;
    std::uint32_t connection_id    = 0;
    std::vector<std::uint8_t> auth_data;  // scramble (usually 20 bytes)
    std::uint32_t capabilities     = 0;
    std::uint8_t  charset          = 0;
    std::uint16_t status_flags     = 0;
    std::string   auth_plugin_name;
};

inline auto parse_server_greeting(const std::uint8_t* data, std::size_t size)
    -> std::expected<server_greeting, std::string>
{
    if (size < 4) return std::unexpected("greeting too short");

    server_greeting g;
    std::size_t pos = 0;

    g.protocol_version = data[pos++];
    if (g.protocol_version != 10)
        return std::unexpected("unsupported protocol version");

    // server version (null-terminated)
    auto sv = read_null_string(data + pos, size - pos);
    if (sv.bytes_consumed == 0) return std::unexpected("bad server version");
    g.server_version = std::string(sv.value);
    pos += sv.bytes_consumed;

    if (pos + 4 > size) return std::unexpected("greeting truncated (conn_id)");
    g.connection_id = read_u32_le(data + pos);
    pos += 4;

    // auth_plugin_data part1 (8 bytes)
    if (pos + 8 > size) return std::unexpected("greeting truncated (auth1)");
    g.auth_data.assign(data + pos, data + pos + 8);
    pos += 8;

    // filler
    if (pos + 1 > size) return std::unexpected("greeting truncated (filler)");
    pos += 1;

    // capabilities low 2 bytes
    if (pos + 2 > size) return std::unexpected("greeting truncated (cap_low)");
    std::uint16_t cap_low = read_u16_le(data + pos); pos += 2;

    // charset, status_flags
    if (pos + 3 > size) return std::unexpected("greeting truncated (charset)");
    g.charset = data[pos++];
    g.status_flags = read_u16_le(data + pos); pos += 2;

    // capabilities high 2 bytes
    if (pos + 2 > size) return std::unexpected("greeting truncated (cap_high)");
    std::uint16_t cap_high = read_u16_le(data + pos); pos += 2;
    g.capabilities = static_cast<std::uint32_t>(cap_low) |
                     (static_cast<std::uint32_t>(cap_high) << 16);

    // auth_plugin_data_len
    if (pos + 1 > size) return std::unexpected("greeting truncated (auth_len)");
    std::uint8_t auth_data_len = data[pos++];

    // reserved (10 bytes)
    if (pos + 10 > size) return std::unexpected("greeting truncated (reserved)");
    pos += 10;

    // auth_plugin_data part2
    if (g.capabilities & CLIENT_SECURE_CONNECTION) {
        std::size_t part2_len = 13;
        if (auth_data_len > 8)
            part2_len = std::max(part2_len, static_cast<std::size_t>(auth_data_len - 8));
        if (pos + part2_len > size)
            part2_len = size - pos;
        std::size_t actual = part2_len;
        if (actual > 0 && data[pos + actual - 1] == 0) --actual;
        g.auth_data.insert(g.auth_data.end(), data + pos, data + pos + actual);
        pos += part2_len;
    }

    // auth_plugin_name (null-terminated)
    if (g.capabilities & CLIENT_PLUGIN_AUTH) {
        auto name = read_null_string(data + pos, size - pos);
        if (name.bytes_consumed > 0) {
            g.auth_plugin_name = std::string(name.value);
            pos += name.bytes_consumed;
        }
    }

    return g;
}

// =============================================================================
// OK packet
// =============================================================================

struct ok_packet {
    std::uint64_t affected_rows  = 0;
    std::uint64_t last_insert_id = 0;
    std::uint16_t status_flags   = 0;
    std::uint16_t warnings       = 0;
    std::string   info;
};

inline auto parse_ok_packet(const std::uint8_t* data, std::size_t size) -> ok_packet {
    ok_packet ok;
    std::size_t pos = 0;
    if (pos < size) ++pos; // skip header (0x00 or 0xFE)

    auto aff = read_lenenc(data + pos, size - pos);
    if (aff.bytes_consumed > 0) { ok.affected_rows = aff.value; pos += aff.bytes_consumed; }

    auto lid = read_lenenc(data + pos, size - pos);
    if (lid.bytes_consumed > 0) { ok.last_insert_id = lid.value; pos += lid.bytes_consumed; }

    if (pos + 2 <= size) { ok.status_flags = read_u16_le(data + pos); pos += 2; }
    if (pos + 2 <= size) { ok.warnings = read_u16_le(data + pos); pos += 2; }

    if (pos < size)
        ok.info = std::string(reinterpret_cast<const char*>(data + pos), size - pos);

    return ok;
}

// =============================================================================
// Error packet
// =============================================================================

struct err_packet {
    std::uint16_t error_code = 0;
    std::string   sql_state;
    std::string   message;
};

inline auto parse_err_packet(const std::uint8_t* data, std::size_t size) -> err_packet {
    err_packet ep;
    std::size_t pos = 0;
    if (pos < size) ++pos; // skip 0xFF

    if (pos + 2 <= size) { ep.error_code = read_u16_le(data + pos); pos += 2; }

    // SQL state marker '#' + 5 chars
    if (pos < size && data[pos] == '#') {
        ++pos;
        if (pos + 5 <= size) {
            ep.sql_state = std::string(reinterpret_cast<const char*>(data + pos), 5);
            pos += 5;
        }
    }

    if (pos < size)
        ep.message = std::string(reinterpret_cast<const char*>(data + pos), size - pos);

    return ep;
}

// =============================================================================
// Column definition
// =============================================================================

inline auto parse_column_def(const std::uint8_t* data, std::size_t size) -> column_meta {
    column_meta col;
    std::size_t pos = 0;

    // catalog ("def")
    auto cat = read_lenenc_string(data + pos, size - pos);
    if (cat.bytes_consumed > 0) pos += cat.bytes_consumed;

    // schema
    auto db = read_lenenc_string(data + pos, size - pos);
    if (db.bytes_consumed > 0) { col.database = std::string(db.value); pos += db.bytes_consumed; }

    // table
    auto tbl = read_lenenc_string(data + pos, size - pos);
    if (tbl.bytes_consumed > 0) { col.table = std::string(tbl.value); pos += tbl.bytes_consumed; }

    // org_table
    auto otbl = read_lenenc_string(data + pos, size - pos);
    if (otbl.bytes_consumed > 0) { col.org_table = std::string(otbl.value); pos += otbl.bytes_consumed; }

    // name
    auto nm = read_lenenc_string(data + pos, size - pos);
    if (nm.bytes_consumed > 0) { col.name = std::string(nm.value); pos += nm.bytes_consumed; }

    // org_name
    auto onm = read_lenenc_string(data + pos, size - pos);
    if (onm.bytes_consumed > 0) { col.org_name = std::string(onm.value); pos += onm.bytes_consumed; }

    // fixed_fields length (lenenc, typically 0x0C)
    auto flen = read_lenenc(data + pos, size - pos);
    if (flen.bytes_consumed > 0) pos += flen.bytes_consumed;

    // charset(2) + column_length(4) + type(1) + flags(2) + decimals(1) + filler(2) = 12
    if (pos + 12 <= size) {
        col.charset       = read_u16_le(data + pos); pos += 2;
        col.column_length  = read_u32_le(data + pos); pos += 4;
        col.type           = static_cast<field_type>(data[pos]); pos += 1;
        col.flags          = read_u16_le(data + pos); pos += 2;
        col.decimals       = data[pos]; pos += 1;
        pos += 2; // filler
    }

    return col;
}

// =============================================================================
// Text protocol row parsing
// =============================================================================

inline auto parse_text_row(
    const std::uint8_t* data, std::size_t size,
    const std::vector<column_meta>& columns
) -> row
{
    row r;
    r.reserve(columns.size());
    std::size_t pos = 0;

    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (pos >= size) { r.push_back(field_value{}); continue; }

        // NULL = 0xFB
        if (data[pos] == 0xFB) {
            r.push_back(field_value{});
            ++pos;
            continue;
        }

        auto sv = read_lenenc_string(data + pos, size - pos);
        if (sv.bytes_consumed == 0) { r.push_back(field_value{}); break; }
        pos += sv.bytes_consumed;

        field_value fv;
        fv.kind_ = compute_field_kind(columns[i].type, columns[i].flags, columns[i].charset);
        std::string raw(sv.value);

        switch (fv.kind_) {
        case field_kind::int64: {
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), fv.int_val);
            (void)ptr; (void)ec;
            break;
        }
        case field_kind::uint64: {
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), fv.uint_val);
            (void)ptr; (void)ec;
            break;
        }
        case field_kind::float_: {
            double tmp = 0;
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), tmp);
            (void)ptr; (void)ec;
            fv.float_val = static_cast<float>(tmp);
            break;
        }
        case field_kind::double_: {
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), fv.double_val);
            (void)ptr; (void)ec;
            break;
        }
        case field_kind::date: {
            // "YYYY-MM-DD"
            unsigned y = 0, m = 0, d = 0;
            if (std::sscanf(raw.c_str(), "%u-%u-%u", &y, &m, &d) >= 3) {
                fv.date_val = {static_cast<std::uint16_t>(y),
                               static_cast<std::uint8_t>(m),
                               static_cast<std::uint8_t>(d)};
            }
            break;
        }
        case field_kind::datetime: {
            // "YYYY-MM-DD HH:MM:SS" or "YYYY-MM-DD HH:MM:SS.ffffff"
            unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, us = 0;
            int n = std::sscanf(raw.c_str(), "%u-%u-%u %u:%u:%u.%u",
                                &y, &mo, &d, &h, &mi, &s, &us);
            if (n >= 6) {
                fv.datetime_val = {static_cast<std::uint16_t>(y),
                                   static_cast<std::uint8_t>(mo),
                                   static_cast<std::uint8_t>(d),
                                   static_cast<std::uint8_t>(h),
                                   static_cast<std::uint8_t>(mi),
                                   static_cast<std::uint8_t>(s),
                                   n >= 7 ? us : 0u};
            } else if (n >= 3) {
                fv.datetime_val.year  = static_cast<std::uint16_t>(y);
                fv.datetime_val.month = static_cast<std::uint8_t>(mo);
                fv.datetime_val.day   = static_cast<std::uint8_t>(d);
            }
            break;
        }
        case field_kind::time: {
            // "[-]HH:MM:SS" or "[-]HH:MM:SS.ffffff"
            const char* p = raw.c_str();
            bool neg = false;
            if (*p == '-') { neg = true; ++p; }
            unsigned h = 0, m = 0, s = 0, us = 0;
            int n = std::sscanf(p, "%u:%u:%u.%u", &h, &m, &s, &us);
            if (n >= 3) {
                fv.time_val = {neg, h,
                               static_cast<std::uint8_t>(m),
                               static_cast<std::uint8_t>(s),
                               n >= 4 ? us : 0u};
            }
            break;
        }
        case field_kind::string:
        case field_kind::blob:
            fv.str_val = std::move(raw);
            break;
        default: // null — shouldn't happen here
            fv.str_val = std::move(raw);
            break;
        }

        r.push_back(std::move(fv));
    }

    return r;
}

// =============================================================================
// Binary protocol row parsing (prepared statement result set)
// =============================================================================

inline auto parse_binary_row(
    const std::uint8_t* data, std::size_t size,
    const std::vector<column_meta>& columns
) -> row
{
    row r;
    r.reserve(columns.size());
    std::size_t pos = 0;
    auto ncols = columns.size();

    // Skip packet header (0x00)
    if (pos >= size) return r;
    ++pos;

    // null bitmap (offset = 2)
    std::size_t bmp_size = null_bitmap_size(ncols, 2);
    if (pos + bmp_size > size) return r;
    const std::uint8_t* bmp = data + pos;
    pos += bmp_size;

    for (std::size_t i = 0; i < ncols; ++i) {
        if (null_bitmap_is_null(bmp, i, 2)) {
            r.push_back(field_value{});
            continue;
        }

        field_value fv;
        bool is_unsigned = (columns[i].flags & column_flags::is_unsigned) != 0;
        fv.kind_ = compute_field_kind(columns[i].type, columns[i].flags, columns[i].charset);

        // Binary protocol wire format determined by field_type
        switch (columns[i].type) {
        case field_type::tiny:
            if (pos + 1 > size) { r.push_back(field_value{}); return r; }
            if (is_unsigned) fv.uint_val = data[pos];
            else             fv.int_val  = static_cast<std::int8_t>(data[pos]);
            pos += 1;
            break;

        case field_type::short_type:
        case field_type::year:
            if (pos + 2 > size) { r.push_back(field_value{}); return r; }
            if (is_unsigned || columns[i].type == field_type::year)
                fv.uint_val = read_u16_le(data + pos);
            else
                fv.int_val = static_cast<std::int16_t>(read_u16_le(data + pos));
            pos += 2;
            break;

        case field_type::long_type:
        case field_type::int24:
            if (pos + 4 > size) { r.push_back(field_value{}); return r; }
            if (is_unsigned) fv.uint_val = read_u32_le(data + pos);
            else             fv.int_val  = static_cast<std::int32_t>(read_u32_le(data + pos));
            pos += 4;
            break;

        case field_type::longlong:
            if (pos + 8 > size) { r.push_back(field_value{}); return r; }
            if (is_unsigned) fv.uint_val = read_u64_le(data + pos);
            else             fv.int_val  = static_cast<std::int64_t>(read_u64_le(data + pos));
            pos += 8;
            break;

        case field_type::bit:
            // BIT: transmitted as lenenc string, interpreted as uint64
            {
                auto bsv = read_lenenc_string(data + pos, size - pos);
                if (bsv.bytes_consumed > 0) {
                    fv.uint_val = 0;
                    for (auto ch : bsv.value)
                        fv.uint_val = (fv.uint_val << 8) | static_cast<unsigned char>(ch);
                    pos += bsv.bytes_consumed;
                }
            }
            break;

        case field_type::float_type:
            if (pos + 4 > size) { r.push_back(field_value{}); return r; }
            fv.float_val = read_float_le(data + pos);
            pos += 4;
            break;

        case field_type::double_type:
            if (pos + 8 > size) { r.push_back(field_value{}); return r; }
            fv.double_val = read_double_le(data + pos);
            pos += 8;
            break;

        case field_type::date: {
            if (pos >= size) { r.push_back(field_value{}); return r; }
            std::uint8_t len = data[pos++];
            if (len >= 4 && pos + len <= size) {
                fv.date_val.year  = read_u16_le(data + pos);
                fv.date_val.month = data[pos + 2];
                fv.date_val.day   = data[pos + 3];
            }
            pos += len;
            break;
        }

        case field_type::datetime:
        case field_type::timestamp: {
            if (pos >= size) { r.push_back(field_value{}); return r; }
            std::uint8_t len = data[pos++];
            if (len >= 4 && pos + len <= size) {
                fv.datetime_val.year  = read_u16_le(data + pos);
                fv.datetime_val.month = data[pos + 2];
                fv.datetime_val.day   = data[pos + 3];
                if (len >= 7) {
                    fv.datetime_val.hour   = data[pos + 4];
                    fv.datetime_val.minute = data[pos + 5];
                    fv.datetime_val.second = data[pos + 6];
                }
                if (len >= 11) {
                    fv.datetime_val.microsecond = read_u32_le(data + pos + 7);
                }
            }
            pos += len;
            break;
        }

        case field_type::time_type: {
            if (pos >= size) { r.push_back(field_value{}); return r; }
            std::uint8_t len = data[pos++];
            if (len >= 8 && pos + len <= size) {
                fv.time_val.negative = data[pos] != 0;
                std::uint32_t days = read_u32_le(data + pos + 1);
                fv.time_val.hours   = days * 24 + data[pos + 5];
                fv.time_val.minutes = data[pos + 6];
                fv.time_val.seconds = data[pos + 7];
                if (len >= 12) {
                    fv.time_val.microsecond = read_u32_le(data + pos + 8);
                }
            }
            pos += len;
            break;
        }

        // String/BLOB/DECIMAL types: lenenc string
        default: {
            auto sv = read_lenenc_string(data + pos, size - pos);
            if (sv.bytes_consumed > 0) {
                fv.str_val = std::string(sv.value);
                pos += sv.bytes_consumed;
            }
            break;
        }
        }

        r.push_back(std::move(fv));
    }

    return r;
}

// =============================================================================
// COM_STMT_PREPARE response parsing
// =============================================================================

struct prepare_stmt_response {
    std::uint32_t stmt_id     = 0;
    std::uint16_t num_columns = 0;
    std::uint16_t num_params  = 0;
    std::uint16_t warnings    = 0;
};

inline auto parse_prepare_stmt_response(const std::uint8_t* data, std::size_t size)
    -> std::expected<prepare_stmt_response, std::string>
{
    // status(1=0x00) + stmt_id(4) + num_columns(2) + num_params(2) + reserved(1) + warning_count(2) = 12
    if (size < 12) return std::unexpected("prepare response too short");

    prepare_stmt_response r;
    std::size_t pos = 0;

    std::uint8_t status = data[pos++];
    if (status != 0x00)
        return std::unexpected("bad prepare response status");

    r.stmt_id     = read_u32_le(data + pos); pos += 4;
    r.num_columns = read_u16_le(data + pos); pos += 2;
    r.num_params  = read_u16_le(data + pos); pos += 2;
    pos += 1; // reserved
    r.warnings    = read_u16_le(data + pos); pos += 2;

    return r;
}

// =============================================================================
// Authentication server response classification
// =============================================================================

enum class handshake_response_type {
    ok, error, auth_switch, auth_more_data, unknown
};

inline auto classify_handshake_response(const std::uint8_t* data, std::size_t size)
    -> handshake_response_type
{
    if (size == 0) return handshake_response_type::unknown;
    switch (data[0]) {
    case OK_HEADER:  return handshake_response_type::ok;
    case ERR_HEADER: return handshake_response_type::error;
    case AUTH_SWITCH: // 0xFE — also EOF, but in handshake context it's auth_switch
        return (size > 1) ? handshake_response_type::auth_switch
                          : handshake_response_type::ok;  // 1-byte 0xFE = OK/EOF
    case AUTH_MORE:
        return (size > 1) ? handshake_response_type::auth_more_data
                          : handshake_response_type::unknown;
    default:
        return handshake_response_type::unknown;
    }
}

// =============================================================================
// AuthSwitch request parsing
// =============================================================================

struct auth_switch_request {
    std::string plugin_name;
    std::vector<std::uint8_t> auth_data;
};

inline auto parse_auth_switch(const std::uint8_t* data, std::size_t size) -> auth_switch_request {
    auth_switch_request r;
    std::size_t pos = 1; // skip 0xFE header

    auto plugin = read_null_string(data + pos, size - pos);
    if (plugin.bytes_consumed > 0) {
        r.plugin_name = std::string(plugin.value);
        pos += plugin.bytes_consumed;
    }

    if (pos < size) {
        std::size_t slen = size - pos;
        if (slen > 0 && data[size - 1] == 0) --slen;
        r.auth_data.assign(data + pos, data + pos + slen);
    }

    return r;
}

} // namespace cnetmod::mysql::detail
