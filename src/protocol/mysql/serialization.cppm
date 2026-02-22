module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.mysql:serialization;

import std;
import :types;
import :protocol;
import :deserialization;

namespace cnetmod::mysql::detail {

// =============================================================================
// Login packet construction
// =============================================================================

inline auto build_login_packet(
    const connect_options& opts,
    const server_greeting& greeting,
    std::uint32_t client_caps,
    const std::vector<std::uint8_t>& auth_response
) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> pkt;
    pkt.reserve(128 + opts.username.size() + opts.database.size() + auth_response.size());

    // capabilities (4 bytes LE)
    std::uint8_t tmp4[4];
    write_u32_le(tmp4, client_caps);
    pkt.insert(pkt.end(), tmp4, tmp4 + 4);

    // max packet size
    write_u32_le(tmp4, static_cast<std::uint32_t>(max_packet_payload));
    pkt.insert(pkt.end(), tmp4, tmp4 + 4);

    // charset (utf8mb4 = 45)
    pkt.push_back(45);

    // reserved (23 zero bytes)
    pkt.insert(pkt.end(), 23, 0);

    // username (null-terminated)
    pkt.insert(pkt.end(), opts.username.begin(), opts.username.end());
    pkt.push_back(0);

    // auth response (length-encoded — we require CLIENT_PLUGIN_AUTH_LENENC)
    if (client_caps & CLIENT_PLUGIN_AUTH_LENENC) {
        write_lenenc(pkt, auth_response.size());
        pkt.insert(pkt.end(), auth_response.begin(), auth_response.end());
    } else if (client_caps & CLIENT_SECURE_CONNECTION) {
        pkt.push_back(static_cast<std::uint8_t>(auth_response.size()));
        pkt.insert(pkt.end(), auth_response.begin(), auth_response.end());
    } else {
        pkt.insert(pkt.end(), auth_response.begin(), auth_response.end());
        pkt.push_back(0);
    }

    // database (null-terminated)
    if (client_caps & CLIENT_CONNECT_WITH_DB) {
        pkt.insert(pkt.end(), opts.database.begin(), opts.database.end());
        pkt.push_back(0);
    }

    // auth plugin name (null-terminated)
    if (client_caps & CLIENT_PLUGIN_AUTH) {
        auto& name = greeting.auth_plugin_name;
        pkt.insert(pkt.end(), name.begin(), name.end());
        pkt.push_back(0);
    }

    return pkt;
}

// =============================================================================
// SSL request packet construction
// =============================================================================

inline auto build_ssl_request(std::uint32_t client_caps) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pkt;
    pkt.reserve(32);

    std::uint8_t tmp4[4];
    write_u32_le(tmp4, client_caps);
    pkt.insert(pkt.end(), tmp4, tmp4 + 4);

    write_u32_le(tmp4, static_cast<std::uint32_t>(max_packet_payload));
    pkt.insert(pkt.end(), tmp4, tmp4 + 4);

    pkt.push_back(45); // charset utf8mb4
    pkt.insert(pkt.end(), 23, 0); // reserved

    return pkt;
}

// =============================================================================
// COM_STMT_PREPARE command construction
// =============================================================================

inline auto build_prepare_stmt_command(std::string_view sql) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pkt;
    pkt.reserve(1 + sql.size());
    pkt.push_back(COM_STMT_PREPARE);
    pkt.insert(pkt.end(),
        reinterpret_cast<const std::uint8_t*>(sql.data()),
        reinterpret_cast<const std::uint8_t*>(sql.data() + sql.size()));
    return pkt;
}

// =============================================================================
// COM_STMT_EXECUTE command construction
// =============================================================================

inline auto build_execute_stmt_command(
    std::uint32_t stmt_id,
    std::span<const param_value> params
) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> pkt;
    pkt.reserve(64 + params.size() * 16);

    // command
    pkt.push_back(COM_STMT_EXECUTE);

    // stmt_id (4)
    std::uint8_t tmp[8];
    write_u32_le(tmp, stmt_id);
    pkt.insert(pkt.end(), tmp, tmp + 4);

    // flags (1) — 0 = CURSOR_TYPE_NO_CURSOR
    pkt.push_back(0x00);

    // iteration_count (4) — always 1
    write_u32_le(tmp, 1);
    pkt.insert(pkt.end(), tmp, tmp + 4);

    if (params.empty()) return pkt;

    // null bitmap (offset = 0)
    std::size_t bmp_sz = null_bitmap_size(params.size(), 0);
    std::size_t bmp_offset = pkt.size();
    pkt.insert(pkt.end(), bmp_sz, 0);

    for (std::size_t i = 0; i < params.size(); ++i) {
        if (params[i].kind == param_value::kind_t::null_kind)
            null_bitmap_set(pkt.data() + bmp_offset, i, 0);
    }

    // new_params_bind_flag = 1 (always send types)
    pkt.push_back(0x01);

    // type info: (field_type(1) + unsigned_flag(1)) * num_params
    for (auto& p : params) {
        pkt.push_back(param_kind_to_field_type(p.kind));
        pkt.push_back(param_kind_unsigned_flag(p.kind));
    }

    // values
    for (auto& p : params) {
        switch (p.kind) {
        case param_value::kind_t::null_kind:
            break; // no data for NULL

        case param_value::kind_t::int64_kind:
            write_u64_le(tmp, static_cast<std::uint64_t>(p.int_val));
            pkt.insert(pkt.end(), tmp, tmp + 8);
            break;

        case param_value::kind_t::uint64_kind:
            write_u64_le(tmp, p.uint_val);
            pkt.insert(pkt.end(), tmp, tmp + 8);
            break;

        case param_value::kind_t::double_kind:
            write_double_le(tmp, p.double_val);
            pkt.insert(pkt.end(), tmp, tmp + 8);
            break;

        case param_value::kind_t::string_kind:
        case param_value::kind_t::blob_kind:
            write_lenenc_string(pkt, p.str_val);
            break;

        case param_value::kind_t::date_kind: {
            // DATE: length(1) + year(2) + month(1) + day(1) = 4 bytes payload
            auto& d = p.date_val;
            if (d.year == 0 && d.month == 0 && d.day == 0) {
                pkt.push_back(0); // zero-length = zero date
            } else {
                pkt.push_back(4);
                std::uint8_t buf[4];
                write_u16_le(buf, d.year);
                pkt.push_back(buf[0]); pkt.push_back(buf[1]);
                pkt.push_back(d.month);
                pkt.push_back(d.day);
            }
            break;
        }

        case param_value::kind_t::datetime_kind: {
            // DATETIME: 0 / 4 / 7 / 11 bytes payload depending on precision
            auto& dt = p.datetime_val;
            if (dt.year == 0 && dt.month == 0 && dt.day == 0 &&
                dt.hour == 0 && dt.minute == 0 && dt.second == 0 &&
                dt.microsecond == 0) {
                pkt.push_back(0);
            } else if (dt.microsecond > 0) {
                pkt.push_back(11);
                std::uint8_t buf[4];
                write_u16_le(buf, dt.year);
                pkt.push_back(buf[0]); pkt.push_back(buf[1]);
                pkt.push_back(dt.month); pkt.push_back(dt.day);
                pkt.push_back(dt.hour); pkt.push_back(dt.minute); pkt.push_back(dt.second);
                write_u32_le(buf, dt.microsecond);
                pkt.insert(pkt.end(), buf, buf + 4);
            } else if (dt.hour > 0 || dt.minute > 0 || dt.second > 0) {
                pkt.push_back(7);
                std::uint8_t buf[2];
                write_u16_le(buf, dt.year);
                pkt.push_back(buf[0]); pkt.push_back(buf[1]);
                pkt.push_back(dt.month); pkt.push_back(dt.day);
                pkt.push_back(dt.hour); pkt.push_back(dt.minute); pkt.push_back(dt.second);
            } else {
                pkt.push_back(4);
                std::uint8_t buf[2];
                write_u16_le(buf, dt.year);
                pkt.push_back(buf[0]); pkt.push_back(buf[1]);
                pkt.push_back(dt.month); pkt.push_back(dt.day);
            }
            break;
        }

        case param_value::kind_t::time_kind: {
            // TIME: 0 / 8 / 12 bytes payload
            auto& t = p.time_val;
            if (t.hours == 0 && t.minutes == 0 && t.seconds == 0 && t.microsecond == 0) {
                pkt.push_back(0);
            } else {
                std::uint32_t days = t.hours / 24;
                std::uint8_t  hh   = static_cast<std::uint8_t>(t.hours % 24);
                if (t.microsecond > 0) {
                    pkt.push_back(12);
                } else {
                    pkt.push_back(8);
                }
                pkt.push_back(t.negative ? 1 : 0);
                std::uint8_t buf[4];
                write_u32_le(buf, days);
                pkt.insert(pkt.end(), buf, buf + 4);
                pkt.push_back(hh);
                pkt.push_back(t.minutes);
                pkt.push_back(t.seconds);
                if (t.microsecond > 0) {
                    write_u32_le(buf, t.microsecond);
                    pkt.insert(pkt.end(), buf, buf + 4);
                }
            }
            break;
        }
        }
    }

    return pkt;
}

// =============================================================================
// COM_STMT_CLOSE command construction
// =============================================================================

inline auto build_close_stmt_command(std::uint32_t stmt_id) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pkt(5);
    pkt[0] = COM_STMT_CLOSE;
    write_u32_le(pkt.data() + 1, stmt_id);
    return pkt;
}

// =============================================================================
// COM_QUERY command construction
// =============================================================================

inline auto build_query_command(std::string_view sql) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> pkt;
    pkt.reserve(1 + sql.size());
    pkt.push_back(COM_QUERY);
    pkt.insert(pkt.end(),
        reinterpret_cast<const std::uint8_t*>(sql.data()),
        reinterpret_cast<const std::uint8_t*>(sql.data() + sql.size()));
    return pkt;
}

} // namespace cnetmod::mysql::detail
