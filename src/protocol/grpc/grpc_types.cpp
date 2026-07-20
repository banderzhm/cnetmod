module cnetmod.protocol.grpc.types;

import std;

namespace cnetmod::grpc {
namespace {
auto lower(std::string_view text) -> std::string {
  std::string out(text);
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}
auto percent_decode(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      unsigned value = 0;
      auto hex = text.substr(i + 1, 2);
      auto [ptr, ec] =
          std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
      if (ec == std::errc{}) {
        out.push_back(static_cast<char>(value));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}
auto header_find(const http::header_map &headers, std::string_view name)
    -> std::string_view {
  if (auto it = headers.find(std::string(name)); it != headers.end())
    return it->second;
  const auto lname = lower(name);
  if (auto it = headers.find(lname); it != headers.end())
    return it->second;
  for (const auto &[key, value] : headers)
    if (lower(key) == lname)
      return value;
  return {};
}
auto metadata_find(const metadata &md, std::string_view name)
    -> std::string_view {
  const auto lname = lower(name);
  for (const auto &[key, value] : md)
    if (lower(key) == lname)
      return value;
  return {};
}
auto base64_encode(std::span<const std::byte> data) -> std::string {
  constexpr std::string_view table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < data.size(); i += 3) {
    std::uint32_t n =
        static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[i]))
        << 16;
    if (i + 1 < data.size())
      n |= static_cast<std::uint32_t>(
               std::to_integer<unsigned char>(data[i + 1]))
           << 8;
    if (i + 2 < data.size())
      n |= std::to_integer<unsigned char>(data[i + 2]);
    out.push_back(table[(n >> 18) & 63]);
    out.push_back(table[(n >> 12) & 63]);
    out.push_back(i + 1 < data.size() ? table[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < data.size() ? table[n & 63] : '=');
  }
  return out;
}
auto base64_decode(std::string_view text) -> std::optional<byte_buffer> {
  auto decode = [](char c) {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '+')
      return 62;
    if (c == '/')
      return 63;
    if (c == '=')
      return -2;
    return -1;
  };
  byte_buffer out;
  std::array<int, 4> quad{};
  std::size_t count = 0;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)))
      continue;
    const int value = decode(c);
    if (value == -1)
      return std::nullopt;
    quad[count++] = value;
    if (count != 4)
      continue;
    if (quad[0] < 0 || quad[1] < 0)
      return std::nullopt;
    std::uint32_t n = static_cast<std::uint32_t>(quad[0]) << 18 |
                      static_cast<std::uint32_t>(quad[1]) << 12;
    if (quad[2] >= 0)
      n |= static_cast<std::uint32_t>(quad[2]) << 6;
    if (quad[3] >= 0)
      n |= static_cast<std::uint32_t>(quad[3]);
    out.push_back(static_cast<std::byte>((n >> 16) & 255));
    if (quad[2] != -2)
      out.push_back(static_cast<std::byte>((n >> 8) & 255));
    if (quad[3] != -2)
      out.push_back(static_cast<std::byte>(n & 255));
    count = 0;
  }
  return count == 0 ? std::optional<byte_buffer>{std::move(out)} : std::nullopt;
}
auto parse_timeout_value(std::string_view text)
    -> std::optional<std::chrono::nanoseconds> {
  if (text.size() < 2 || text.size() > 9)
    return std::nullopt;
  std::uint64_t value{};
  const auto value_text = text.substr(0, text.size() - 1);
  auto [ptr, ec] = std::from_chars(
      value_text.data(), value_text.data() + value_text.size(), value);
  if (ec != std::errc{} || ptr != value_text.data() + value_text.size())
    return std::nullopt;
  switch (text.back()) {
  case 'H':
    return std::chrono::hours(value);
  case 'M':
    return std::chrono::minutes(value);
  case 'S':
    return std::chrono::seconds(value);
  case 'm':
    return std::chrono::milliseconds(value);
  case 'u':
    return std::chrono::microseconds(value);
  case 'n':
    return std::chrono::nanoseconds(value);
  default:
    return std::nullopt;
  }
}
} // namespace

auto status::ok() const noexcept -> bool { return code == status_code::ok; }
auto call_context::deadline_exceeded() const noexcept -> bool {
  return timeout.count() > 0 &&
         std::chrono::steady_clock::now() - started > timeout;
}
auto service_path(std::string_view service, std::string_view method)
    -> std::string {
  return "/" + std::string(service) + "/" + std::string(method);
}
auto make_status(status_code code, std::string message, metadata trailers)
    -> status {
  return status{.code = code,
                .message = std::move(message),
                .trailers = std::move(trailers)};
}
auto compression_name(compression_algorithm algorithm) -> std::string_view {
  return algorithm == compression_algorithm::gzip ? "gzip" : "identity";
}
auto compression_from_header(std::string_view text)
    -> std::optional<compression_algorithm> {
  auto value = lower(text);
  if (value.empty() || value == "identity")
    return compression_algorithm::identity;
  if (value == "gzip")
    return compression_algorithm::gzip;
  return std::nullopt;
}
auto accepts_compression(std::string_view header,
                         compression_algorithm algorithm) -> bool {
  if (algorithm == compression_algorithm::identity)
    return true;
  const auto wanted = compression_name(algorithm);
  std::size_t start{};
  while (start <= header.size()) {
    const auto comma = header.find(',', start);
    auto token = header.substr(start, comma == std::string_view::npos
                                          ? header.size() - start
                                          : comma - start);
    while (!token.empty() &&
           std::isspace(static_cast<unsigned char>(token.front())))
      token.remove_prefix(1);
    while (!token.empty() &&
           std::isspace(static_cast<unsigned char>(token.back())))
      token.remove_suffix(1);
    if (lower(token) == wanted)
      return true;
    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  return false;
}
auto parse_service_path(std::string_view path)
    -> std::optional<std::pair<std::string, std::string>> {
  if (path.empty() || path.front() != '/')
    return std::nullopt;
  path.remove_prefix(1);
  const auto slash = path.rfind('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= path.size())
    return std::nullopt;
  return std::pair{std::string(path.substr(0, slash)),
                   std::string(path.substr(slash + 1))};
}
auto metadata_from_headers(const http::header_map &headers) -> metadata {
  metadata out;
  for (const auto &[key, value] : headers) {
    auto name = lower(key);
    if (name.starts_with(":") || name == "content-type" ||
        name == "content-length" || name == "te" || name == "grpc-status" ||
        name == "grpc-message")
      continue;
    out.emplace(std::move(name), value);
  }
  return out;
}
auto header_value(const http::header_map &headers, std::string_view name)
    -> std::string_view {
  return header_find(headers, name);
}
auto metadata_value(const metadata &md, std::string_view name)
    -> std::string_view {
  return metadata_find(md, name);
}
auto metadata_wire_size(const metadata &md) noexcept -> std::size_t {
  std::size_t total{};
  for (const auto &[key, value] : md)
    total += key.size() + value.size() + 4;
  return total;
}
void append_metadata_headers(http::request &req, const metadata &md) {
  for (const auto &[key, value] : md)
    req.append_header(key, value);
}
void append_metadata_headers(http::response &response, const metadata &md) {
  for (const auto &[key, value] : md)
    response.append_header(key, value);
}
void append_metadata_trailers(http::response &response, const metadata &md) {
  for (const auto &[key, value] : md)
    response.append_trailer(key, value);
}
auto status_from_headers(const http::header_map &headers) -> status {
  status out;
  const auto code_text = header_find(headers, "grpc-status");
  if (code_text.empty())
    return make_status(status_code::unknown, "missing grpc-status");
  int code{};
  auto [ptr, ec] = std::from_chars(code_text.data(),
                                   code_text.data() + code_text.size(), code);
  if (ec != std::errc{})
    return make_status(status_code::unknown, "invalid grpc-status");
  out.code = static_cast<status_code>(code);
  out.message = percent_decode(header_find(headers, "grpc-message"));
  return out;
}
auto status_from_response(const http::response &response) -> status {
  return !header_find(response.trailers(), "grpc-status").empty()
             ? status_from_headers(response.trailers())
             : status_from_headers(response.headers());
}
auto timeout_from_headers(const http::header_map &headers)
    -> std::chrono::milliseconds {
  const auto text = header_find(headers, "grpc-timeout");
  auto value = parse_timeout_value(text);
  return value ? std::chrono::duration_cast<std::chrono::milliseconds>(*value)
               : std::chrono::milliseconds{};
}
auto format_timeout(std::chrono::milliseconds timeout) -> std::string {
  if (timeout.count() <= 0)
    return {};
  const auto ms = timeout.count();
  if (ms <= 99'999'999)
    return std::to_string(ms) + "m";
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
  if (seconds <= 99'999'999)
    return std::to_string(seconds) + "S";
  const auto minutes =
      std::chrono::duration_cast<std::chrono::minutes>(timeout).count();
  if (minutes <= 99'999'999)
    return std::to_string(minutes) + "M";
  return std::to_string(std::min<std::int64_t>(
             std::chrono::duration_cast<std::chrono::hours>(timeout).count(),
             99'999'999)) +
         "H";
}
void add_binary_metadata(metadata &md, std::string key,
                         std::span<const std::byte> value) {
  if (!key.ends_with("-bin"))
    key += "-bin";
  md.emplace(std::move(key), base64_encode(value));
}
auto get_binary_metadata(const metadata &md, std::string_view key)
    -> std::vector<byte_buffer> {
  std::string lookup(key);
  if (!lookup.ends_with("-bin"))
    lookup += "-bin";
  std::vector<byte_buffer> out;
  const auto [first, last] = md.equal_range(lookup);
  for (auto it = first; it != last; ++it)
    if (auto decoded = base64_decode(it->second))
      out.push_back(std::move(*decoded));
  return out;
}

} // namespace cnetmod::grpc
