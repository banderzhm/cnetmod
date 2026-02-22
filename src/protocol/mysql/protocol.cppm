module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.mysql:protocol;

import std;
import :types;

namespace cnetmod::mysql {

// =============================================================================
// Constants
// =============================================================================

inline constexpr std::size_t  max_packet_payload  = 0xFFFFFF;  // 16 MB - 1
inline constexpr std::size_t  packet_header_size  = 4;
inline constexpr std::size_t  scramble_size       = 20;

// Packet header identifiers
inline constexpr std::uint8_t OK_HEADER    = 0x00;
inline constexpr std::uint8_t EOF_HEADER   = 0xFE;
inline constexpr std::uint8_t ERR_HEADER   = 0xFF;
inline constexpr std::uint8_t AUTH_SWITCH  = 0xFE;
inline constexpr std::uint8_t AUTH_MORE    = 0x01;

// MySQL command codes
inline constexpr std::uint8_t COM_QUIT              = 0x01;
inline constexpr std::uint8_t COM_QUERY             = 0x03;
inline constexpr std::uint8_t COM_PING              = 0x0E;
inline constexpr std::uint8_t COM_STMT_PREPARE      = 0x16;
inline constexpr std::uint8_t COM_STMT_EXECUTE      = 0x17;
inline constexpr std::uint8_t COM_STMT_CLOSE        = 0x19;
inline constexpr std::uint8_t COM_RESET_CONNECTION  = 0x1F;

// Capability flags
inline constexpr std::uint32_t CLIENT_LONG_PASSWORD          = 1;
inline constexpr std::uint32_t CLIENT_FOUND_ROWS             = 2;
inline constexpr std::uint32_t CLIENT_LONG_FLAG              = 4;
inline constexpr std::uint32_t CLIENT_CONNECT_WITH_DB        = 8;
inline constexpr std::uint32_t CLIENT_NO_SCHEMA              = 16;
inline constexpr std::uint32_t CLIENT_COMPRESS               = 32;
inline constexpr std::uint32_t CLIENT_LOCAL_FILES            = 128;
inline constexpr std::uint32_t CLIENT_IGNORE_SPACE           = 256;
inline constexpr std::uint32_t CLIENT_PROTOCOL_41            = 0x200;
inline constexpr std::uint32_t CLIENT_INTERACTIVE            = 0x400;
inline constexpr std::uint32_t CLIENT_SSL                    = 0x800;
inline constexpr std::uint32_t CLIENT_TRANSACTIONS           = 0x2000;
inline constexpr std::uint32_t CLIENT_SECURE_CONNECTION      = 0x8000;
inline constexpr std::uint32_t CLIENT_MULTI_STATEMENTS       = (1u << 16);
inline constexpr std::uint32_t CLIENT_MULTI_RESULTS          = (1u << 17);
inline constexpr std::uint32_t CLIENT_PS_MULTI_RESULTS       = (1u << 18);
inline constexpr std::uint32_t CLIENT_PLUGIN_AUTH            = (1u << 19);
inline constexpr std::uint32_t CLIENT_CONNECT_ATTRS          = (1u << 20);
inline constexpr std::uint32_t CLIENT_PLUGIN_AUTH_LENENC     = (1u << 21);
inline constexpr std::uint32_t CLIENT_SESSION_TRACK          = (1u << 23);
inline constexpr std::uint32_t CLIENT_DEPRECATE_EOF          = (1u << 24);

// =============================================================================
// Little-endian read/write
// =============================================================================

namespace detail {

inline auto read_u16_le(const std::uint8_t* p) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

inline auto read_u24_le(const std::uint8_t* p) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16);
}

inline auto read_u32_le(const std::uint8_t* p) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline auto read_u64_le(const std::uint8_t* p) noexcept -> std::uint64_t {
    std::uint64_t v = 0;
    std::memcpy(&v, p, 8);  // LE platforms (x86/ARM LE)
    return v;
}

inline auto read_float_le(const std::uint8_t* p) noexcept -> float {
    float v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline auto read_double_le(const std::uint8_t* p) noexcept -> double {
    double v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

inline void write_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

inline void write_u24_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
}

inline void write_u32_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

inline void write_u64_le(std::uint8_t* p, std::uint64_t v) noexcept {
    std::memcpy(p, &v, 8);
}

inline void write_float_le(std::uint8_t* p, float v) noexcept {
    std::memcpy(p, &v, 4);
}

inline void write_double_le(std::uint8_t* p, double v) noexcept {
    std::memcpy(p, &v, 8);
}

// =============================================================================
// length-encoded integer read/write
// =============================================================================

struct lenenc_result {
    std::uint64_t value          = 0;
    std::size_t   bytes_consumed = 0;  // 0 = error
};

inline auto read_lenenc(const std::uint8_t* p, std::size_t avail) noexcept -> lenenc_result {
    if (avail == 0) return {};
    std::uint8_t first = p[0];
    if (first < 0xFB) {
        return {first, 1};
    } else if (first == 0xFC && avail >= 3) {
        return {read_u16_le(p + 1), 3};
    } else if (first == 0xFD && avail >= 4) {
        return {read_u24_le(p + 1), 4};
    } else if (first == 0xFE && avail >= 9) {
        return {read_u64_le(p + 1), 9};
    }
    return {};
}

inline void write_lenenc(std::vector<std::uint8_t>& buf, std::uint64_t v) {
    if (v < 0xFB) {
        buf.push_back(static_cast<std::uint8_t>(v));
    } else if (v <= 0xFFFF) {
        buf.push_back(0xFC);
        std::uint8_t tmp[2];
        write_u16_le(tmp, static_cast<std::uint16_t>(v));
        buf.insert(buf.end(), tmp, tmp + 2);
    } else if (v <= 0xFFFFFF) {
        buf.push_back(0xFD);
        std::uint8_t tmp[3];
        write_u24_le(tmp, static_cast<std::uint32_t>(v));
        buf.insert(buf.end(), tmp, tmp + 3);
    } else {
        buf.push_back(0xFE);
        std::uint8_t tmp[8];
        write_u64_le(tmp, v);
        buf.insert(buf.end(), tmp, tmp + 8);
    }
}

// =============================================================================
// length-encoded string
// =============================================================================

struct lenenc_string_result {
    std::string_view value;
    std::size_t      bytes_consumed = 0;
};

inline auto read_lenenc_string(const std::uint8_t* p, std::size_t avail) noexcept
    -> lenenc_string_result
{
    auto len = read_lenenc(p, avail);
    if (len.bytes_consumed == 0) return {};
    if (len.bytes_consumed + len.value > avail) return {};
    return {
        std::string_view(
            reinterpret_cast<const char*>(p + len.bytes_consumed),
            static_cast<std::size_t>(len.value)),
        len.bytes_consumed + static_cast<std::size_t>(len.value)
    };
}

inline void write_lenenc_string(std::vector<std::uint8_t>& buf, std::string_view s) {
    write_lenenc(buf, s.size());
    buf.insert(buf.end(),
        reinterpret_cast<const std::uint8_t*>(s.data()),
        reinterpret_cast<const std::uint8_t*>(s.data() + s.size()));
}

// =============================================================================
// null-terminated string
// =============================================================================

inline auto read_null_string(const std::uint8_t* p, std::size_t avail) noexcept
    -> lenenc_string_result
{
    auto* end = static_cast<const std::uint8_t*>(std::memchr(p, 0, avail));
    if (!end) return {};
    auto len = static_cast<std::size_t>(end - p);
    return {std::string_view(reinterpret_cast<const char*>(p), len), len + 1};
}

// =============================================================================
// null bitmap utilities
// =============================================================================

/// Calculate null bitmap size
/// offset: for binary result rows = 2, for COM_STMT_EXECUTE params = 0
inline auto null_bitmap_size(std::size_t num_fields, std::size_t offset) noexcept
    -> std::size_t
{
    return (num_fields + 7 + offset) / 8;
}

/// Check if a field in null bitmap is NULL
inline auto null_bitmap_is_null(
    const std::uint8_t* bitmap,
    std::size_t field_index,
    std::size_t offset
) noexcept -> bool
{
    std::size_t byte_pos = (field_index + offset) / 8;
    std::size_t bit_pos  = (field_index + offset) % 8;
    return (bitmap[byte_pos] & (1u << bit_pos)) != 0;
}

/// Set a field in null bitmap to NULL
inline void null_bitmap_set(
    std::uint8_t* bitmap,
    std::size_t field_index,
    std::size_t offset
) noexcept
{
    std::size_t byte_pos = (field_index + offset) / 8;
    std::size_t bit_pos  = (field_index + offset) % 8;
    bitmap[byte_pos] |= static_cast<std::uint8_t>(1u << bit_pos);
}

// =============================================================================
// param_value → protocol_field_type mapping
// =============================================================================

inline auto param_kind_to_field_type(param_value::kind_t k) noexcept -> std::uint8_t {
    switch (k) {
    case param_value::kind_t::null_kind:     return static_cast<std::uint8_t>(field_type::null_type);
    case param_value::kind_t::int64_kind:    return static_cast<std::uint8_t>(field_type::longlong);
    case param_value::kind_t::uint64_kind:   return static_cast<std::uint8_t>(field_type::longlong);
    case param_value::kind_t::double_kind:   return static_cast<std::uint8_t>(field_type::double_type);
    case param_value::kind_t::string_kind:   return static_cast<std::uint8_t>(field_type::var_string);
    case param_value::kind_t::blob_kind:     return static_cast<std::uint8_t>(field_type::blob);
    case param_value::kind_t::date_kind:     return static_cast<std::uint8_t>(field_type::date);
    case param_value::kind_t::datetime_kind: return static_cast<std::uint8_t>(field_type::datetime);
    case param_value::kind_t::time_kind:     return static_cast<std::uint8_t>(field_type::time_type);
    default: return static_cast<std::uint8_t>(field_type::null_type);
    }
}

inline auto param_kind_unsigned_flag(param_value::kind_t k) noexcept -> std::uint8_t {
    return k == param_value::kind_t::uint64_kind ? 0x80 : 0x00;
}

} // namespace detail

} // namespace cnetmod::mysql
