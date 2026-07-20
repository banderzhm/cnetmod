export module cnetmod.protocol.mysql:protocol;

import std;
import :types;

namespace cnetmod::mysql {
inline constexpr std::size_t max_packet_payload = 0xFFFFFF,
                             packet_header_size = 4, scramble_size = 20;
inline constexpr std::uint8_t OK_HEADER = 0x00, EOF_HEADER = 0xFE,
                              ERR_HEADER = 0xFF, AUTH_SWITCH = 0xFE,
                              AUTH_MORE = 0x01;
inline constexpr std::uint8_t COM_QUIT = 0x01, COM_QUERY = 0x03,
                              COM_PING = 0x0E, COM_STMT_PREPARE = 0x16,
                              COM_STMT_EXECUTE = 0x17, COM_STMT_CLOSE = 0x19,
                              COM_RESET_CONNECTION = 0x1F;
inline constexpr std::uint32_t CLIENT_LONG_PASSWORD = 1, CLIENT_FOUND_ROWS = 2,
                               CLIENT_LONG_FLAG = 4, CLIENT_CONNECT_WITH_DB = 8,
                               CLIENT_NO_SCHEMA = 16, CLIENT_COMPRESS = 32,
                               CLIENT_LOCAL_FILES = 128,
                               CLIENT_IGNORE_SPACE = 256,
                               CLIENT_PROTOCOL_41 = 0x200,
                               CLIENT_INTERACTIVE = 0x400, CLIENT_SSL = 0x800,
                               CLIENT_TRANSACTIONS = 0x2000,
                               CLIENT_SECURE_CONNECTION = 0x8000,
                               CLIENT_MULTI_STATEMENTS = (1u << 16),
                               CLIENT_MULTI_RESULTS = (1u << 17),
                               CLIENT_PS_MULTI_RESULTS = (1u << 18),
                               CLIENT_PLUGIN_AUTH = (1u << 19),
                               CLIENT_CONNECT_ATTRS = (1u << 20),
                               CLIENT_PLUGIN_AUTH_LENENC = (1u << 21),
                               CLIENT_SESSION_TRACK = (1u << 23),
                               CLIENT_DEPRECATE_EOF = (1u << 24);

namespace detail {
auto read_u16_le(const std::uint8_t *) noexcept -> std::uint16_t;
auto read_u24_le(const std::uint8_t *) noexcept -> std::uint32_t;
auto read_u32_le(const std::uint8_t *) noexcept -> std::uint32_t;
auto read_u64_le(const std::uint8_t *) noexcept -> std::uint64_t;
auto read_float_le(const std::uint8_t *) noexcept -> float;
auto read_double_le(const std::uint8_t *) noexcept -> double;
void write_u16_le(std::uint8_t *, std::uint16_t) noexcept;
void write_u24_le(std::uint8_t *, std::uint32_t) noexcept;
void write_u32_le(std::uint8_t *, std::uint32_t) noexcept;
void write_u64_le(std::uint8_t *, std::uint64_t) noexcept;
void write_float_le(std::uint8_t *, float) noexcept;
void write_double_le(std::uint8_t *, double) noexcept;

struct lenenc_result {
  std::uint64_t value{};
  std::size_t bytes_consumed{};
};

auto read_lenenc(const std::uint8_t *, std::size_t) noexcept -> lenenc_result;
void write_lenenc(std::vector<std::uint8_t> &, std::uint64_t);

struct lenenc_string_result {
  std::string_view value;
  std::size_t bytes_consumed{};
};

auto read_lenenc_string(const std::uint8_t *, std::size_t) noexcept
    -> lenenc_string_result;
void write_lenenc_string(std::vector<std::uint8_t> &, std::string_view);
auto read_null_string(const std::uint8_t *, std::size_t) noexcept
    -> lenenc_string_result;
auto null_bitmap_size(std::size_t, std::size_t) noexcept -> std::size_t;
auto null_bitmap_is_null(const std::uint8_t *, std::size_t,
                         std::size_t) noexcept -> bool;
void null_bitmap_set(std::uint8_t *, std::size_t, std::size_t) noexcept;
auto param_kind_to_field_type(param_value::kind_t) noexcept -> std::uint8_t;
auto param_kind_unsigned_flag(param_value::kind_t) noexcept -> std::uint8_t;
} // namespace detail
} // namespace cnetmod::mysql