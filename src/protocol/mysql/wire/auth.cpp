module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_SSL
#include <openssl/sha.h>
#endif

module cnetmod.protocol.mysql;
import :auth;

namespace cnetmod::mysql::detail {
#ifdef CNETMOD_HAS_SSL
auto hash_native_password(std::string_view password,
                          std::span<const std::uint8_t> scramble)
    -> std::vector<std::uint8_t> {
  if (password.empty())
    return {};
  std::array<std::uint8_t, SHA_DIGEST_LENGTH> first{};
  std::array<std::uint8_t, SHA_DIGEST_LENGTH> second{};
  std::array<std::uint8_t, SHA_DIGEST_LENGTH> salted_hash{};
  SHA1(reinterpret_cast<const unsigned char *>(password.data()),
       password.size(), first.data());
  SHA1(first.data(), first.size(), second.data());
  std::vector<std::uint8_t> salted;
  salted.reserve(scramble.size() + second.size());
  salted.insert(salted.end(), scramble.begin(), scramble.end());
  salted.insert(salted.end(), second.begin(), second.end());
  SHA1(salted.data(), salted.size(), salted_hash.data());
  std::vector<std::uint8_t> result(SHA_DIGEST_LENGTH);
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = first[i] ^ salted_hash[i];
  return result;
}

auto hash_caching_sha2(std::string_view password,
                       std::span<const std::uint8_t> scramble)
    -> std::vector<std::uint8_t> {
  if (password.empty())
    return {};
  std::array<std::uint8_t, SHA256_DIGEST_LENGTH> first{};
  std::array<std::uint8_t, SHA256_DIGEST_LENGTH> second{};
  std::array<std::uint8_t, SHA256_DIGEST_LENGTH> salted_hash{};
  SHA256(reinterpret_cast<const unsigned char *>(password.data()),
         password.size(), first.data());
  SHA256(first.data(), first.size(), second.data());
  std::vector<std::uint8_t> salted;
  salted.reserve(second.size() + scramble.size());
  salted.insert(salted.end(), second.begin(), second.end());
  salted.insert(salted.end(), scramble.begin(), scramble.end());
  SHA256(salted.data(), salted.size(), salted_hash.data());
  std::vector<std::uint8_t> result(SHA256_DIGEST_LENGTH);
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = first[i] ^ salted_hash[i];
  return result;
}
#else
auto hash_native_password(std::string_view, std::span<const std::uint8_t>)
    -> std::vector<std::uint8_t> {
  return {};
}
auto hash_caching_sha2(std::string_view, std::span<const std::uint8_t>)
    -> std::vector<std::uint8_t> {
  return {};
}
#endif

auto hash_password_for_plugin(std::string_view plugin_name,
                              std::string_view password,
                              std::span<const std::uint8_t> scramble)
    -> std::vector<std::uint8_t> {
  return plugin_name == PLUGIN_CSHA2P
             ? hash_caching_sha2(password, scramble)
             : hash_native_password(password, scramble);
}
} // namespace cnetmod::mysql::detail