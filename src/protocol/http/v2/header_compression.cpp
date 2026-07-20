module cnetmod.protocol.http.v2.header_compression;

import std;
import cnetmod.protocol.http.v2.huffman;

namespace cnetmod::http::v2 {
namespace {
struct static_entry {
  std::string_view name;
  std::string_view value;
};

constexpr static_entry static_table[] = {
    {},
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""},
};

void encode_integer(std::vector<std::byte> &out, std::uint32_t value,
                    unsigned prefix, std::uint8_t head = 0) {
  const auto mask = (1u << prefix) - 1u;
  if (value < mask) {
    out.push_back(static_cast<std::byte>(head | value));
    return;
  }
  out.push_back(static_cast<std::byte>(head | mask));
  for (value -= mask; value >= 128; value >>= 7)
    out.push_back(static_cast<std::byte>((value & 0x7f) | 0x80));
  out.push_back(static_cast<std::byte>(value));
}

auto decode_integer(std::span<const std::byte> input, std::size_t &offset,
                    unsigned prefix) -> std::optional<std::uint32_t> {
  if (offset == input.size())
    return std::nullopt;
  const auto mask = (1u << prefix) - 1u;
  auto value =
      std::uint32_t{std::to_integer<std::uint8_t>(input[offset++]) & mask};
  if (value < mask)
    return value;
  unsigned shift = 0;
  while (offset < input.size() && shift <= 28) {
    const auto byte = std::to_integer<std::uint8_t>(input[offset++]);
    if (shift == 28 && byte > 0x7f)
      return std::nullopt;
    value += static_cast<std::uint32_t>(byte & 0x7f) << shift;
    if (!(byte & 0x80))
      return value;
    shift += 7;
  }
  return std::nullopt;
}

void encode_string(std::vector<std::byte> &out, std::string_view value) {
  auto encoded = huffman_encode(value);
  if (encoded.size() < value.size()) {
    encode_integer(out, static_cast<std::uint32_t>(encoded.size()), 7, 0x80);
    out.insert(out.end(), encoded.begin(), encoded.end());
    return;
  }
  encode_integer(out, static_cast<std::uint32_t>(value.size()), 7);
  for (char ch : value)
    out.push_back(static_cast<std::byte>(ch));
}

auto decode_string(std::span<const std::byte> input, std::size_t &offset)
    -> std::optional<std::string> {
  if (offset == input.size())
    return std::nullopt;
  const bool huffman =
      (std::to_integer<std::uint8_t>(input[offset]) & 0x80) != 0;
  auto size = decode_integer(input, offset, 7);
  if (!size || *size > input.size() - offset)
    return std::nullopt;
  const auto value = input.subspan(offset, *size);
  offset += *size;
  if (huffman) {
    auto decoded = huffman_decode(value);
    if (!decoded)
      return std::nullopt;
    return std::move(*decoded);
  }
  return std::string(reinterpret_cast<const char *>(value.data()),
                     value.size());
}
} // namespace

header_compression::header_compression(std::size_t limit)
    : dynamic_table_limit_(limit) {}

void header_compression::evict() {
  while (dynamic_table_bytes_ > dynamic_table_limit_ &&
         !dynamic_table_.empty()) {
    const auto &item = dynamic_table_.back();
    dynamic_table_bytes_ -= item.name.size() + item.value.size() + 32;
    dynamic_table_.pop_back();
  }
}

void header_compression::insert(entry item) {
  const auto bytes = item.name.size() + item.value.size() + 32;
  if (bytes > dynamic_table_limit_) {
    dynamic_table_.clear();
    dynamic_table_bytes_ = 0;
    return;
  }
  dynamic_table_.push_front(std::move(item));
  dynamic_table_bytes_ += bytes;
  evict();
}

auto header_compression::at(std::uint32_t index) const -> std::optional<entry> {
  if (index > 0 && index < std::size(static_table))
    return entry{std::string(static_table[index].name),
                 std::string(static_table[index].value)};
  const auto dynamic =
      static_cast<std::size_t>(index) - std::size(static_table);
  if (index >= std::size(static_table) && dynamic < dynamic_table_.size())
    return dynamic_table_[dynamic];
  return std::nullopt;
}

auto header_compression::find(std::string_view name, std::string_view value,
                              bool exact) const -> std::uint32_t {
  for (std::uint32_t i = 1; i < std::size(static_table); ++i)
    if (static_table[i].name == name &&
        (!exact || static_table[i].value == value))
      return i;
  for (std::size_t i = 0; i < dynamic_table_.size(); ++i)
    if (dynamic_table_[i].name == name &&
        (!exact || dynamic_table_[i].value == value))
      return static_cast<std::uint32_t>(std::size(static_table) + i);
  return 0;
}

void header_compression::set_dynamic_table_limit(std::size_t bytes) {
  dynamic_table_limit_ = bytes;
  evict();
}

auto header_compression::dynamic_table_limit() const noexcept -> std::size_t {
  return dynamic_table_limit_;
}

auto header_compression::decode(std::span<const std::byte> block)
    -> std::expected<std::vector<header_field>, std::error_code> {
  std::vector<header_field> result;
  std::size_t offset = 0;
  bool can_resize = true;
  while (offset < block.size()) {
    const auto first = std::to_integer<std::uint8_t>(block[offset]);
    if (first & 0x80) {
      can_resize = false;
      auto index = decode_integer(block, offset, 7);
      auto value = index ? at(*index) : std::nullopt;
      if (!value)
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
      result.push_back({std::move(value->name), std::move(value->value)});
      continue;
    }
    if ((first & 0xe0) == 0x20) {
      if (!can_resize)
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
      auto size = decode_integer(block, offset, 5);
      if (!size)
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
      set_dynamic_table_limit(*size);
      continue;
    }
    can_resize = false;
    const bool incremental = (first & 0xc0) == 0x40;
    const bool sensitive = (first & 0xf0) == 0x10;
    auto index = decode_integer(block, offset, incremental ? 6 : 4);
    if (!index)
      return std::unexpected(std::make_error_code(std::errc::protocol_error));
    std::string name;
    if (*index) {
      auto value = at(*index);
      if (!value)
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
      name = std::move(value->name);
    } else {
      auto value = decode_string(block, offset);
      if (!value || value->empty())
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
      name = std::move(*value);
    }
    auto value = decode_string(block, offset);
    if (!value)
      return std::unexpected(std::make_error_code(std::errc::protocol_error));
    header_field field{std::move(name), std::move(*value), sensitive};
    if (incremental)
      insert({field.name, field.value});
    result.push_back(std::move(field));
  }
  return result;
}

auto header_compression::encode(std::span<const header_field> fields)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  std::vector<std::byte> result;
  for (const auto &field : fields) {
    if (field.name.empty())
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (const auto exact = find(field.name, field.value, true)) {
      encode_integer(result, exact, 7, 0x80);
      continue;
    }
    const auto indexed = !field.sensitive && field.name != "authorization" &&
                         field.name != "cookie" && field.name != "set-cookie";
    const auto name = find(field.name, {}, false);
    encode_integer(result, name, indexed ? 6 : 4,
                   indexed ? 0x40 : (field.sensitive ? 0x10 : 0));
    if (!name)
      encode_string(result, field.name);
    encode_string(result, field.value);
    if (indexed)
      insert({field.name, field.value});
  }
  return result;
}
} // namespace cnetmod::http::v2