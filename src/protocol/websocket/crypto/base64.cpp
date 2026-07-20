module cnetmod.protocol.websocket; // implementation unit
import :base64;

namespace cnetmod::ws::detail {
namespace {
constexpr char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr auto valid(unsigned char c) noexcept -> bool {
  return c == '+' || (c >= '/' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

constexpr auto index(unsigned char c) noexcept -> unsigned char {
  if (c >= 'A' && c <= 'Z')
    return static_cast<unsigned char>(c - 'A');
  if (c >= 'a' && c <= 'z')
    return static_cast<unsigned char>(c - 'a' + 26);
  if (c >= '0' && c <= '9')
    return static_cast<unsigned char>(c - '0' + 52);
  return c == '+'   ? static_cast<unsigned char>(62)
         : c == '/' ? static_cast<unsigned char>(63)
                    : static_cast<unsigned char>(0);
}
} // namespace

auto base64_encode(const void *input, std::size_t length) -> std::string {
  std::string out;
  out.reserve((length + 2) / 3 * 4);
  auto source = static_cast<const unsigned char *>(input);
  for (std::size_t offset = 0; offset < length; offset += 3) {
    const auto remain = length - offset;
    const auto a = source[offset];
    const auto b =
        remain > 1 ? source[offset + 1] : static_cast<unsigned char>(0);
    const auto c =
        remain > 2 ? source[offset + 2] : static_cast<unsigned char>(0);
    out += alphabet[a >> 2];
    out += alphabet[((a & 3) << 4) | (b >> 4)];
    out += remain > 1 ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
    out += remain > 2 ? alphabet[c & 63] : '=';
  }
  return out;
}

auto base64_encode(std::span<const std::byte> input) -> std::string {
  return base64_encode(input.data(), input.size());
}

auto base64_decode(std::string_view input) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  out.reserve(input.size() * 3 / 4);
  unsigned char quartet[4]{};
  auto count = 0;
  for (const auto ch : input) {
    if (ch == '=')
      break;
    if (!valid(static_cast<unsigned char>(ch)))
      break;
    quartet[count++] = static_cast<unsigned char>(ch);
    if (count != 4)
      continue;
    const auto a = index(quartet[0]), b = index(quartet[1]),
               c = index(quartet[2]), d = index(quartet[3]);
    out.push_back(static_cast<std::byte>((a << 2) | (b >> 4)));
    out.push_back(static_cast<std::byte>((b << 4) | (c >> 2)));
    out.push_back(static_cast<std::byte>((c << 6) | d));
    count = 0;
  }
  if (count > 1) {
    for (auto i = count; i < 4; ++i)
      quartet[i] = 0;
    const auto a = index(quartet[0]), b = index(quartet[1]),
               c = index(quartet[2]);
    out.push_back(static_cast<std::byte>((a << 2) | (b >> 4)));
    if (count > 2)
      out.push_back(static_cast<std::byte>((b << 4) | (c >> 2)));
  }
  return out;
}
} // namespace cnetmod::ws::detail
