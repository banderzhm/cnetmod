module;

#include <cnetmod/config.hpp>
#include <cstring>

module cnetmod.protocol.http;

import std;
import :semantics;
import :multipart_parser;
import :multipart_url;
import :multipart_content_type;
import :multipart_disposition;

namespace cnetmod::http {
namespace {
auto skip_ows(std::string_view s) noexcept -> std::string_view {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  return s;
}
auto hex_digit(char c) noexcept -> int {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
auto base64_decode(std::string_view input)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<std::byte> out;
  out.reserve(input.size() * 3 / 4);
  std::uint32_t accum{};
  int bits{};
  for (const auto c : input) {
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
      continue;
    if (c == '=')
      break;
    const auto p = alphabet.find(c);
    if (p == std::string_view::npos)
      return std::unexpected(make_error_code(http_errc::unsupported_encoding));
    accum = (accum << 6) | static_cast<std::uint32_t>(p);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::byte>((accum >> bits) & 0xff));
    }
  }
  return out;
}
auto quoted_printable_decode(std::string_view input)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  std::vector<std::byte> out;
  out.reserve(input.size());
  for (std::size_t i{}; i < input.size(); ++i) {
    if (input[i] == '=') {
      if (i + 1 < input.size() && input[i + 1] == '\n') {
        ++i;
        continue;
      }
      if (i + 2 < input.size() && input[i + 1] == '\r' &&
          input[i + 2] == '\n') {
        i += 2;
        continue;
      }
      if (i + 2 < input.size()) {
        const auto hi = hex_digit(input[i + 1]), lo = hex_digit(input[i + 2]);
        if (hi >= 0 && lo >= 0) {
          out.push_back(static_cast<std::byte>((hi << 4) | lo));
          i += 2;
          continue;
        }
      }
    }
    out.push_back(static_cast<std::byte>(input[i]));
  }
  return out;
}
} // namespace

multipart_parser::multipart_parser(std::string boundary)
    : boundary_(std::move(boundary)), delimiter_("--" + boundary_),
      close_delimiter_("--" + boundary_ + "--") {}
auto multipart_parser::find_in(std::string_view data, std::string_view needle,
                               std::size_t offset) noexcept -> std::size_t {
  if (offset >= data.size() || needle.empty())
    return std::string_view::npos;
  const auto haystack = data.substr(offset);
  const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(),
                              needle.end());
  return it == haystack.end()
             ? std::string_view::npos
             : offset + static_cast<std::size_t>(it - haystack.begin());
}
auto multipart_parser::skip_transport_padding(std::string_view body,
                                              std::size_t &pos) const -> bool {
  while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'))
    ++pos;
  if (pos + 1 < body.size() && body[pos] == '\r' && body[pos + 1] == '\n') {
    pos += 2;
    return true;
  }
  if (pos < body.size() && body[pos] == '\n') {
    ++pos;
    return true;
  }
  if (pos + 1 < body.size() && body[pos] == '-' && body[pos + 1] == '-')
    return true;
  return pos >= body.size();
}
auto multipart_parser::parse(std::string_view body)
    -> std::expected<form_data, std::error_code> {
  form_data result;
  const auto first = find_in(body, delimiter_, 0);
  if (first == std::string_view::npos)
    return std::unexpected(make_error_code(http_errc::invalid_multipart));
  auto pos = first + delimiter_.size();
  if (!skip_transport_padding(body, pos))
    return std::unexpected(make_error_code(http_errc::invalid_multipart));
  while (pos < body.size()) {
    const auto next = find_in(body, "\r\n" + delimiter_, pos);
    std::string_view part;
    bool last = false;
    if (next == std::string_view::npos) {
      const auto close = find_in(body, "\r\n" + close_delimiter_, pos);
      part = close == std::string_view::npos ? body.substr(pos)
                                             : body.substr(pos, close - pos);
      last = true;
    } else
      part = body.substr(pos, next - pos);
    if (auto parsed = parse_part(part, result); !parsed)
      return std::unexpected(parsed.error());
    if (last)
      break;
    pos = next + 2 + delimiter_.size();
    if (pos + 1 < body.size() && body[pos] == '-' && body[pos + 1] == '-')
      break;
    if (!skip_transport_padding(body, pos))
      break;
  }
  return result;
}
auto multipart_parser::parse_part_headers(std::string_view block)
    -> std::expected<header_map, std::error_code> {
  header_map headers;
  std::string key, value;
  auto flush = [&] {
    if (!key.empty()) {
      if (auto it = headers.find(key); it != headers.end()) {
        it->second += ", ";
        it->second += value;
      } else
        headers.emplace(std::move(key), std::move(value));
      key.clear();
      value.clear();
    }
  };
  while (!block.empty()) {
    const auto nl = block.find('\n');
    auto line = block.substr(0, nl);
    block = nl == std::string_view::npos ? std::string_view{}
                                         : block.substr(nl + 1);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.empty())
      continue;
    if (line.front() == ' ' || line.front() == '\t') {
      if (!key.empty()) {
        value += ' ';
        value += skip_ows(line);
      }
      continue;
    }
    flush();
    const auto colon = line.find(':');
    if (colon == std::string_view::npos)
      continue;
    auto raw_key = line.substr(0, colon);
    while (!raw_key.empty() &&
           (raw_key.back() == ' ' || raw_key.back() == '\t'))
      raw_key.remove_suffix(1);
    auto raw_value = skip_ows(line.substr(colon + 1));
    while (!raw_value.empty() &&
           (raw_value.back() == ' ' || raw_value.back() == '\t'))
      raw_value.remove_suffix(1);
    key = std::string(raw_key);
    value = std::string(raw_value);
  }
  flush();
  return headers;
}
auto multipart_parser::raw_copy(std::string_view body)
    -> std::vector<std::byte> {
  std::vector<std::byte> result(body.size());
  if (!body.empty())
    std::memcpy(result.data(), body.data(), body.size());
  return result;
}
auto multipart_parser::decode_body(std::string_view body,
                                   std::string_view encoding)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  if (encoding.empty())
    return raw_copy(body);
  std::string normalized(encoding);
  for (auto &c : normalized)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + 32);
  if (normalized == "7bit" || normalized == "8bit" || normalized == "binary")
    return raw_copy(body);
  if (normalized == "base64")
    return base64_decode(body);
  if (normalized == "quoted-printable")
    return quoted_printable_decode(body);
  return std::unexpected(make_error_code(http_errc::unsupported_encoding));
}
auto multipart_parser::parse_part(std::string_view data, form_data &result)
    -> std::expected<void, std::error_code> {
  auto end = find_in(data, "\r\n\r\n", 0);
  std::string_view heads, body;
  if (end == std::string_view::npos) {
    end = find_in(data, "\n\n", 0);
    if (end == std::string_view::npos)
      body = data;
    else {
      heads = data.substr(0, end);
      body = data.substr(end + 2);
    }
  } else {
    heads = data.substr(0, end);
    body = data.substr(end + 4);
  }
  auto headers = parse_part_headers(heads);
  if (!headers)
    return std::unexpected(headers.error());
  const auto disposition = headers->find("Content-Disposition");
  if (disposition == headers->end())
    return {};
  auto parsed = parse_content_disposition(disposition->second);
  if (parsed.name.empty() && parsed.type != "form-data")
    return {};
  const auto transfer = headers->find("Content-Transfer-Encoding");
  const auto decoded = decode_body(
      body, transfer == headers->end() ? std::string_view{}
                                       : std::string_view{transfer->second});
  if (!decoded)
    return std::unexpected(decoded.error());
  if (parsed.has_filename()) {
    form_file file;
    file.field_name = std::move(parsed.name);
    file.filename = std::string(parsed.effective_filename());
    file.headers = std::move(*headers);
    if (const auto type = file.headers.find("Content-Type");
        type != file.headers.end())
      file.content_type = type->second;
    else
      file.content_type = "application/octet-stream";
    file.data = std::move(*decoded);
    result.add_file(std::move(file));
  } else {
    result.add_field(
        {std::move(parsed.name),
         std::string(reinterpret_cast<const char *>(decoded->data()),
                     decoded->size())});
  }
  return {};
}
auto parse_form_urlencoded(std::string_view body) -> form_data {
  form_data result;
  while (!body.empty()) {
    const auto amp = body.find('&');
    const auto pair = body.substr(0, amp);
    if (!pair.empty()) {
      const auto equal = pair.find('=');
      result.add_field({url_decode(pair.substr(0, equal)),
                        equal == std::string_view::npos
                            ? std::string{}
                            : url_decode(pair.substr(equal + 1))});
    }
    if (amp == std::string_view::npos)
      break;
    body.remove_prefix(amp + 1);
  }
  return result;
}
auto parse_form(std::string_view header, std::string_view body)
    -> std::expected<form_data, std::error_code> {
  const auto type = parse_content_type(header);
  if (type.mime == "multipart/form-data") {
    const auto boundary = type.param("boundary");
    if (boundary.empty())
      return std::unexpected(make_error_code(http_errc::missing_boundary));
    return multipart_parser{std::string(boundary)}.parse(body);
  }
  if (type.mime == "application/x-www-form-urlencoded")
    return parse_form_urlencoded(body);
  return std::unexpected(make_error_code(http_errc::invalid_multipart));
}
} // namespace cnetmod::http
