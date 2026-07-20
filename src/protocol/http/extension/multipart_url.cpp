module cnetmod.protocol.http;
import std;
import :multipart_url;
namespace cnetmod::http {
namespace {
auto hex(char c) noexcept -> int {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}
auto unreserved(char c) noexcept -> bool {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
         c == '.' || c == '~';
}
} // namespace
auto url_decode(std::string_view in, bool plus) -> std::string {
  std::string out;
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      auto a = hex(in[i + 1]), b = hex(in[i + 2]);
      if (a >= 0 && b >= 0) {
        out += static_cast<char>((a << 4) | b);
        i += 2;
        continue;
      }
    }
    out += plus && in[i] == '+' ? ' ' : in[i];
  }
  return out;
}
auto url_encode(std::string_view in) -> std::string {
  static constexpr char chars[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(in.size() * 3 / 2);
  for (auto c : in) {
    if (unreserved(c))
      out += c;
    else {
      auto u = static_cast<unsigned char>(c);
      out += '%';
      out += chars[u >> 4];
      out += chars[u & 15];
    }
  }
  return out;
}
} // namespace cnetmod::http
