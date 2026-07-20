module cnetmod.protocol.websocket; // implementation unit
import :sha1;

namespace cnetmod::ws::detail {
namespace {
auto rol(std::uint32_t value, unsigned count) noexcept -> std::uint32_t {
  return (value << count) | (value >> (32 - count));
}

void process(std::uint32_t state[5], const unsigned char *block) noexcept {
  std::uint32_t words[80]{};
  for (auto i = 0; i < 16; ++i)
    words[i] = (std::uint32_t(block[i * 4]) << 24) |
               (std::uint32_t(block[i * 4 + 1]) << 16) |
               (std::uint32_t(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
  for (auto i = 16; i < 80; ++i)
    words[i] =
        rol(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
  auto a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
  for (auto i = 0; i < 80; ++i) {
    const auto f = i < 20   ? ((b & c) | (~b & d))
                   : i < 40 ? (b ^ c ^ d)
                   : i < 60 ? ((b & c) | (b & d) | (c & d))
                            : (b ^ c ^ d);
    const auto k = i < 20   ? 0x5A827999u
                   : i < 40 ? 0x6ED9EBA1u
                   : i < 60 ? 0x8F1BBCDCu
                            : 0xCA62C1D6u;
    const auto next = rol(a, 5) + f + e + k + words[i];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = next;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}
} // namespace

auto sha1(const void *source, std::size_t length) noexcept
    -> std::array<std::byte, 20> {
  auto bytes = static_cast<const unsigned char *>(source);
  std::uint32_t state[5]{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                         0xC3D2E1F0u};
  std::size_t offset = 0;
  while (length - offset >= 64) {
    process(state, bytes + offset);
    offset += 64;
  }
  std::array<unsigned char, 128> tail{};
  const auto remaining = length - offset;
  for (std::size_t i = 0; i < remaining; ++i)
    tail[i] = bytes[offset + i];
  tail[remaining] = 0x80;
  const auto blocks = remaining >= 56 ? 2 : 1;
  const auto bit_length = static_cast<std::uint64_t>(length) * 8;
  for (auto i = 0; i < 8; ++i)
    tail[blocks * 64 - 1 - i] =
        static_cast<unsigned char>(bit_length >> (i * 8));
  for (auto i = 0; i < blocks; ++i)
    process(state, tail.data() + i * 64);
  std::array<std::byte, 20> result{};
  for (auto i = 0; i < 20; ++i)
    result[i] = static_cast<std::byte>(state[i / 4] >> ((3 - (i % 4)) * 8));
  return result;
}

auto sha1(std::span<const std::byte> input) noexcept
    -> std::array<std::byte, 20> {
  return sha1(input.data(), input.size());
}

auto sha1(std::string_view input) noexcept -> std::array<std::byte, 20> {
  return sha1(input.data(), input.size());
}
} // namespace cnetmod::ws::detail
