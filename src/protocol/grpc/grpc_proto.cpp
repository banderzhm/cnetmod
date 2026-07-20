module cnetmod.protocol.grpc.proto;

import std;
import cnetmod.core.error;

namespace cnetmod::grpc::proto {
namespace {
auto protocol_error() -> std::expected<std::vector<field>, std::error_code> {
  return std::unexpected(make_error_code(std::errc::protocol_error));
}
void put_varint(byte_buffer &out, std::uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<std::byte>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<std::byte>(value));
}
auto read_varint(std::span<const std::byte> data, std::size_t &pos)
    -> std::optional<std::uint64_t> {
  std::uint64_t result{};
  for (unsigned shift = 0; pos < data.size() && shift <= 63; shift += 7) {
    const auto byte = std::to_integer<std::uint8_t>(data[pos++]);
    result |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0)
      return result;
  }
  return std::nullopt;
}
auto strip_comments(std::string_view input) -> std::string {
  std::string out;
  for (std::size_t i{}; i < input.size();) {
    if (i + 1 < input.size() && input[i] == '/' && input[i + 1] == '/') {
      while (i < input.size() && input[i] != '\n')
        ++i;
    } else if (i + 1 < input.size() && input[i] == '/' && input[i + 1] == '*') {
      i += 2;
      while (i + 1 < input.size() && !(input[i] == '*' && input[i + 1] == '/'))
        ++i;
      i = std::min(input.size(), i + 2);
    } else
      out.push_back(input[i++]);
  }
  return out;
}
class schema_parser {
public:
  explicit schema_parser(std::string source) : source_(std::move(source)) {}
  auto parse() -> std::expected<file_def, std::error_code> {
    file_def file;
    while (true) {
      skip();
      if (eof())
        return file;
      if (take("syntax")) {
        if (!take("="))
          return fail();
        auto value = string();
        if (!value)
          return fail();
        file.syntax = std::move(*value);
        take(";");
      } else if (take("package")) {
        auto name = ident();
        if (!name)
          return fail();
        file.package = std::move(*name);
        take(";");
      } else if (take("message")) {
        auto value = message();
        if (!value)
          return fail();
        file.messages.push_back(std::move(*value));
      } else if (take("service")) {
        auto value = service();
        if (!value)
          return fail();
        file.services.push_back(std::move(*value));
      } else
        skip_statement();
    }
  }

private:
  auto eof() const -> bool { return pos_ >= source_.size(); }
  void skip() {
    while (!eof() && std::isspace(static_cast<unsigned char>(source_[pos_])))
      ++pos_;
  }
  auto take(std::string_view token) -> bool {
    skip();
    if (source_.substr(pos_, token.size()) != token)
      return false;
    const auto next = pos_ + token.size();
    if (!token.empty() &&
        (std::isalnum(static_cast<unsigned char>(token.back())) ||
         token.back() == '_') &&
        next < source_.size() &&
        (std::isalnum(static_cast<unsigned char>(source_[next])) ||
         source_[next] == '_'))
      return false;
    pos_ = next;
    return true;
  }
  auto ident() -> std::optional<std::string> {
    skip();
    if (eof() || !(std::isalpha(static_cast<unsigned char>(source_[pos_])) ||
                   source_[pos_] == '_'))
      return std::nullopt;
    const auto start = pos_++;
    while (!eof() && (std::isalnum(static_cast<unsigned char>(source_[pos_])) ||
                      source_[pos_] == '_' || source_[pos_] == '.'))
      ++pos_;
    return source_.substr(start, pos_ - start);
  }
  auto string() -> std::optional<std::string> {
    skip();
    if (eof() || source_[pos_] != '\"')
      return std::nullopt;
    ++pos_;
    std::string out;
    while (!eof() && source_[pos_] != '\"') {
      if (source_[pos_] == '\\' && pos_ + 1 < source_.size())
        ++pos_;
      out.push_back(source_[pos_++]);
    }
    if (eof())
      return std::nullopt;
    ++pos_;
    return out;
  }
  auto number() -> std::optional<std::uint32_t> {
    skip();
    const auto begin = pos_;
    while (!eof() && std::isdigit(static_cast<unsigned char>(source_[pos_])))
      ++pos_;
    if (begin == pos_)
      return std::nullopt;
    std::uint32_t value{};
    const auto text = std::string_view(source_).substr(begin, pos_ - begin);
    auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} ? std::optional{value} : std::nullopt;
  }
  auto message() -> std::optional<message_def> {
    auto name = ident();
    if (!name || !take("{"))
      return std::nullopt;
    message_def out{.name = std::move(*name)};
    while (!eof() && !take("}")) {
      if (take("message") || take("enum") || take("oneof")) {
        block();
        continue;
      }
      if (take("option") || take("reserved") || take("extensions")) {
        skip_statement();
        continue;
      }
      field_def f;
      auto first = ident();
      if (!first) {
        skip_statement();
        continue;
      }
      if (*first == "repeated" || *first == "optional" ||
          *first == "required") {
        f.label = std::move(*first);
        auto type = ident();
        if (!type)
          return std::nullopt;
        f.type = std::move(*type);
      } else
        f.type = std::move(*first);
      auto field_name = ident();
      if (!field_name || !take("="))
        return std::nullopt;
      auto field_number = number();
      if (!field_number)
        return std::nullopt;
      f.name = std::move(*field_name);
      f.number = *field_number;
      skip_statement();
      out.fields.push_back(std::move(f));
    }
    return out;
  }
  auto service() -> std::optional<service_def> {
    auto name = ident();
    if (!name || !take("{"))
      return std::nullopt;
    service_def out{.name = std::move(*name)};
    while (!eof() && !take("}")) {
      if (!take("rpc")) {
        skip_statement();
        continue;
      }
      auto name = ident();
      if (!name || !take("("))
        return std::nullopt;
      rpc_def rpc{.name = std::move(*name)};
      rpc.client_streaming = take("stream");
      auto request = ident();
      if (!request || !take(")") || !take("returns") || !take("("))
        return std::nullopt;
      rpc.request_type = std::move(*request);
      rpc.server_streaming = take("stream");
      auto response = ident();
      if (!response || !take(")"))
        return std::nullopt;
      rpc.response_type = std::move(*response);
      if (take("{"))
        block_body();
      else
        take(";");
      out.rpcs.push_back(std::move(rpc));
    }
    return out;
  }
  void skip_statement() {
    skip();
    if (!eof() && source_[pos_] == '{') {
      block();
      return;
    }
    while (!eof() && source_[pos_] != ';' && source_[pos_] != '}')
      ++pos_;
    if (!eof() && source_[pos_] == ';')
      ++pos_;
  }
  void block() {
    skip();
    if (!eof() && source_[pos_] == '{')
      ++pos_;
    block_body();
  }
  void block_body() {
    int depth = 1;
    while (!eof() && depth > 0) {
      if (source_[pos_] == '{')
        ++depth;
      else if (source_[pos_] == '}')
        --depth;
      ++pos_;
    }
  }
  auto fail() const -> std::expected<file_def, std::error_code> {
    return std::unexpected(make_error_code(std::errc::invalid_argument));
  }
  std::string source_;
  std::size_t pos_{};
};
} // namespace

auto parse_schema(std::string_view text)
    -> std::expected<file_def, std::error_code> {
  return schema_parser(strip_comments(text)).parse();
}
auto encode_varint(std::uint64_t value) -> byte_buffer {
  byte_buffer out;
  put_varint(out, value);
  return out;
}
auto decode_varint(std::span<const std::byte> data, std::size_t &pos)
    -> std::optional<std::uint64_t> {
  return read_varint(data, pos);
}
auto zigzag_encode(std::int64_t value) noexcept -> std::uint64_t {
  return (static_cast<std::uint64_t>(value) << 1) ^
         static_cast<std::uint64_t>(value >> 63);
}
auto zigzag_decode(std::uint64_t value) noexcept -> std::int64_t {
  return static_cast<std::int64_t>((value >> 1) ^ (~(value & 1) + 1));
}
void append_key(byte_buffer &out, std::uint32_t number, wire_type type) {
  put_varint(out, static_cast<std::uint64_t>(number) << 3 |
                      static_cast<std::uint8_t>(type));
}
void append_uint64(byte_buffer &out, std::uint32_t number,
                   std::uint64_t value) {
  append_key(out, number, wire_type::varint);
  put_varint(out, value);
}
void append_int64(byte_buffer &out, std::uint32_t number, std::int64_t value) {
  append_uint64(out, number, static_cast<std::uint64_t>(value));
}
void append_sint64(byte_buffer &out, std::uint32_t number, std::int64_t value) {
  append_uint64(out, number, zigzag_encode(value));
}
void append_bool(byte_buffer &out, std::uint32_t number, bool value) {
  append_uint64(out, number, value ? 1 : 0);
}
void append_bytes(byte_buffer &out, std::uint32_t number,
                  std::span<const std::byte> value) {
  append_key(out, number, wire_type::length_delimited);
  put_varint(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
}
void append_string(byte_buffer &out, std::uint32_t number,
                   std::string_view value) {
  append_bytes(
      out, number,
      {reinterpret_cast<const std::byte *>(value.data()), value.size()});
}
auto decode_message(std::span<const std::byte> data)
    -> std::expected<std::vector<field>, std::error_code> {
  std::vector<field> out;
  std::size_t pos{};
  while (pos < data.size()) {
    auto key = read_varint(data, pos);
    if (!key || *key >> 3 == 0)
      return protocol_error();
    field f{.number = static_cast<std::uint32_t>(*key >> 3),
            .type = static_cast<wire_type>(*key & 7)};
    if (f.type == wire_type::varint) {
      auto value = read_varint(data, pos);
      if (!value)
        return protocol_error();
      f.varint_value = *value;
    } else if (f.type == wire_type::fixed64) {
      if (pos + 8 > data.size())
        return protocol_error();
      for (unsigned i{}; i < 8; ++i)
        f.fixed64_value |= static_cast<std::uint64_t>(
                               std::to_integer<std::uint8_t>(data[pos + i]))
                           << (8 * i);
      pos += 8;
    } else if (f.type == wire_type::length_delimited) {
      auto length = read_varint(data, pos);
      if (!length || *length > data.size() - pos)
        return protocol_error();
      f.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                     data.begin() + static_cast<std::ptrdiff_t>(pos + *length));
      pos += static_cast<std::size_t>(*length);
    } else if (f.type == wire_type::fixed32) {
      if (pos + 4 > data.size())
        return protocol_error();
      for (unsigned i{}; i < 4; ++i)
        f.fixed32_value |= static_cast<std::uint32_t>(
                               std::to_integer<std::uint8_t>(data[pos + i]))
                           << (8 * i);
      pos += 4;
    } else
      return protocol_error();
    out.push_back(std::move(f));
  }
  return out;
}
auto find_first(std::span<const field> fields, std::uint32_t number)
    -> const field * {
  auto it = std::ranges::find_if(
      fields, [number](const field &value) { return value.number == number; });
  return it == fields.end() ? nullptr : &*it;
}
auto field_string(const field &f) -> std::optional<std::string> {
  return f.type == wire_type::length_delimited
             ? std::optional{std::string(
                   reinterpret_cast<const char *>(f.bytes.data()),
                   f.bytes.size())}
             : std::nullopt;
}
auto field_bytes(const field &f) -> std::optional<byte_buffer> {
  return f.type == wire_type::length_delimited ? std::optional{f.bytes}
                                               : std::nullopt;
}
auto field_uint64(const field &f) -> std::optional<std::uint64_t> {
  return f.type == wire_type::varint ? std::optional{f.varint_value}
                                     : std::nullopt;
}
auto field_bool(const field &f) -> std::optional<bool> {
  return f.type == wire_type::varint ? std::optional{f.varint_value != 0}
                                     : std::nullopt;
}
auto find_message(const file_def &file, std::string_view name)
    -> const message_def * {
  auto it = std::ranges::find_if(
      file.messages, [name](const auto &value) { return value.name == name; });
  return it == file.messages.end() ? nullptr : &*it;
}
auto find_service(const file_def &file, std::string_view name)
    -> const service_def * {
  auto it = std::ranges::find_if(
      file.services, [name](const auto &value) { return value.name == name; });
  return it == file.services.end() ? nullptr : &*it;
}
auto find_rpc(const service_def &service, std::string_view name)
    -> const rpc_def * {
  auto it = std::ranges::find_if(
      service.rpcs, [name](const auto &value) { return value.name == name; });
  return it == service.rpcs.end() ? nullptr : &*it;
}
auto rpc_path(const file_def &file, const service_def &service,
              const rpc_def &rpc) -> std::string {
  auto full =
      file.package.empty() ? service.name : file.package + "." + service.name;
  return cnetmod::grpc::service_path(full, rpc.name);
}
auto rpc_kind(const rpc_def &rpc) noexcept -> cnetmod::grpc::call_kind {
  return rpc.client_streaming
             ? (rpc.server_streaming
                    ? cnetmod::grpc::call_kind::bidi_streaming
                    : cnetmod::grpc::call_kind::client_streaming)
             : (rpc.server_streaming
                    ? cnetmod::grpc::call_kind::server_streaming
                    : cnetmod::grpc::call_kind::unary);
}

} // namespace cnetmod::grpc::proto
