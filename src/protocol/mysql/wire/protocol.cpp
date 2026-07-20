module;
#include <cstring>
module cnetmod.protocol.mysql;
import :protocol;

namespace cnetmod::mysql::detail {
auto read_u16_le(const std::uint8_t *p) noexcept -> std::uint16_t {
  return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}

auto read_u24_le(const std::uint8_t *p) noexcept -> std::uint32_t {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16);
}

auto read_u32_le(const std::uint8_t *p) noexcept -> std::uint32_t {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

auto read_u64_le(const std::uint8_t *p) noexcept -> std::uint64_t {
  std::uint64_t value{};
  std::memcpy(&value, p, 8);
  return value;
}

auto read_float_le(const std::uint8_t *p) noexcept -> float {
  float value{};
  std::memcpy(&value, p, 4);
  return value;
}

auto read_double_le(const std::uint8_t *p) noexcept -> double {
  double value{};
  std::memcpy(&value, p, 8);
  return value;
}

void write_u16_le(std::uint8_t *p, std::uint16_t v) noexcept {
  p[0] = std::uint8_t(v);
  p[1] = std::uint8_t(v >> 8);
}

void write_u24_le(std::uint8_t *p, std::uint32_t v) noexcept {
  p[0] = std::uint8_t(v);
  p[1] = std::uint8_t(v >> 8);
  p[2] = std::uint8_t(v >> 16);
}

void write_u32_le(std::uint8_t *p, std::uint32_t v) noexcept {
  p[0] = std::uint8_t(v);
  p[1] = std::uint8_t(v >> 8);
  p[2] = std::uint8_t(v >> 16);
  p[3] = std::uint8_t(v >> 24);
}

void write_u64_le(std::uint8_t *p, std::uint64_t v) noexcept {
  std::memcpy(p, &v, 8);
}
void write_float_le(std::uint8_t *p, float v) noexcept {
  std::memcpy(p, &v, 4);
}
void write_double_le(std::uint8_t *p, double v) noexcept {
  std::memcpy(p, &v, 8);
}

auto read_lenenc(const std::uint8_t *p, std::size_t avail) noexcept
    -> lenenc_result {
  if (!avail)
    return {};
  if (p[0] < 0xfb)
    return {p[0], 1};
  if (p[0] == 0xfc && avail >= 3)
    return {read_u16_le(p + 1), 3};
  if (p[0] == 0xfd && avail >= 4)
    return {read_u24_le(p + 1), 4};
  if (p[0] == 0xfe && avail >= 9)
    return {read_u64_le(p + 1), 9};
  return {};
}

void write_lenenc(std::vector<std::uint8_t> &b, std::uint64_t v) {
  if (v < 0xfb)
    b.push_back(std::uint8_t(v));
  else if (v <= 0xffff) {
    b.push_back(0xfc);
    auto n = b.size();
    b.resize(n + 2);
    write_u16_le(b.data() + n, std::uint16_t(v));
  } else if (v <= 0xffffff) {
    b.push_back(0xfd);
    auto n = b.size();
    b.resize(n + 3);
    write_u24_le(b.data() + n, std::uint32_t(v));
  } else {
    b.push_back(0xfe);
    auto n = b.size();
    b.resize(n + 8);
    write_u64_le(b.data() + n, v);
  }
}

auto read_lenenc_string(const std::uint8_t *p, std::size_t avail) noexcept
    -> lenenc_string_result {
  auto len = read_lenenc(p, avail);
  if (!len.bytes_consumed || len.bytes_consumed + len.value > avail)
    return {};
  return {
      std::string_view(reinterpret_cast<const char *>(p + len.bytes_consumed),
                       std::size_t(len.value)),
      len.bytes_consumed + std::size_t(len.value)};
}

void write_lenenc_string(std::vector<std::uint8_t> &b, std::string_view s) {
  write_lenenc(b, s.size());
  b.insert(b.end(), reinterpret_cast<const std::uint8_t *>(s.data()),
           reinterpret_cast<const std::uint8_t *>(s.data() + s.size()));
}

auto read_null_string(const std::uint8_t *p, std::size_t avail) noexcept
    -> lenenc_string_result {
  auto end = static_cast<const std::uint8_t *>(std::memchr(p, 0, avail));
  if (!end)
    return {};
  auto n = std::size_t(end - p);
  return {std::string_view(reinterpret_cast<const char *>(p), n), n + 1};
}

auto null_bitmap_size(std::size_t fields, std::size_t offset) noexcept
    -> std::size_t {
  return (fields + 7 + offset) / 8;
}

auto null_bitmap_is_null(const std::uint8_t *b, std::size_t field,
                         std::size_t offset) noexcept -> bool {
  return (b[(field + offset) / 8] & (1u << ((field + offset) % 8))) != 0;
}

void null_bitmap_set(std::uint8_t *b, std::size_t field,
                     std::size_t offset) noexcept {
  b[(field + offset) / 8] |= std::uint8_t(1u << ((field + offset) % 8));
}

auto param_kind_to_field_type(param_value::kind_t k) noexcept -> std::uint8_t {
  switch (k) {
  case param_value::kind_t::null_kind:
    return std::uint8_t(field_type::null_type);
  case param_value::kind_t::int64_kind:
  case param_value::kind_t::uint64_kind:
    return std::uint8_t(field_type::longlong);
  case param_value::kind_t::double_kind:
    return std::uint8_t(field_type::double_type);
  case param_value::kind_t::string_kind:
    return std::uint8_t(field_type::var_string);
  case param_value::kind_t::blob_kind:
    return std::uint8_t(field_type::blob);
  case param_value::kind_t::date_kind:
    return std::uint8_t(field_type::date);
  case param_value::kind_t::datetime_kind:
    return std::uint8_t(field_type::datetime);
  case param_value::kind_t::time_kind:
    return std::uint8_t(field_type::time_type);
  }
  return std::uint8_t(field_type::null_type);
}

auto param_kind_unsigned_flag(param_value::kind_t k) noexcept -> std::uint8_t {
  return k == param_value::kind_t::uint64_kind ? 0x80 : 0;
}
} // namespace cnetmod::mysql::detail