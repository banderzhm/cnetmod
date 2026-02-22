module;

#include <cnetmod/config.hpp>
#include <cstring>

#ifdef CNETMOD_HAS_SSL
#include <openssl/sha.h>
#endif

export module cnetmod.protocol.mysql:auth;

import std;
import :protocol;

namespace cnetmod::mysql::detail {

// =============================================================================
// mysql_native_password
// SHA1(password) XOR SHA1(scramble + SHA1(SHA1(password)))
// =============================================================================

#ifdef CNETMOD_HAS_SSL

inline auto hash_native_password(
    std::string_view password,
    std::span<const std::uint8_t> scramble
) -> std::vector<std::uint8_t>
{
    if (password.empty()) return {};

    // SHA1(password)
    std::array<std::uint8_t, SHA_DIGEST_LENGTH> sha1_pwd{};
    SHA1(reinterpret_cast<const unsigned char*>(password.data()),
         password.size(), sha1_pwd.data());

    // SHA1(SHA1(password))
    std::array<std::uint8_t, SHA_DIGEST_LENGTH> sha1_sha1_pwd{};
    SHA1(sha1_pwd.data(), sha1_pwd.size(), sha1_sha1_pwd.data());

    // SHA1(scramble + SHA1(SHA1(password)))
    std::vector<std::uint8_t> salted;
    salted.reserve(scramble.size() + SHA_DIGEST_LENGTH);
    salted.insert(salted.end(), scramble.begin(), scramble.end());
    salted.insert(salted.end(), sha1_sha1_pwd.begin(), sha1_sha1_pwd.end());

    std::array<std::uint8_t, SHA_DIGEST_LENGTH> sha1_salted{};
    SHA1(salted.data(), salted.size(), sha1_salted.data());

    // XOR
    std::vector<std::uint8_t> result(SHA_DIGEST_LENGTH);
    for (std::size_t i = 0; i < SHA_DIGEST_LENGTH; ++i)
        result[i] = sha1_pwd[i] ^ sha1_salted[i];

    return result;
}

// =============================================================================
// caching_sha2_password (MySQL 8+ default)
// SHA256(password) XOR SHA256( SHA256(SHA256(password)) + scramble )
// =============================================================================

inline auto hash_caching_sha2(
    std::string_view password,
    std::span<const std::uint8_t> scramble
) -> std::vector<std::uint8_t>
{
    if (password.empty()) return {};

    // SHA256(password)
    std::array<std::uint8_t, SHA256_DIGEST_LENGTH> password_sha{};
    SHA256(reinterpret_cast<const unsigned char*>(password.data()),
           password.size(), password_sha.data());

    // SHA256(SHA256(password)) = double_sha
    std::array<std::uint8_t, SHA256_DIGEST_LENGTH> double_sha{};
    SHA256(password_sha.data(), password_sha.size(), double_sha.data());

    // SHA256(double_sha + scramble)
    std::vector<std::uint8_t> buffer;
    buffer.reserve(SHA256_DIGEST_LENGTH + scramble.size());
    buffer.insert(buffer.end(), double_sha.begin(), double_sha.end());
    buffer.insert(buffer.end(), scramble.begin(), scramble.end());

    std::array<std::uint8_t, SHA256_DIGEST_LENGTH> salted{};
    SHA256(buffer.data(), buffer.size(), salted.data());

    // XOR: password_sha ^ salted
    std::vector<std::uint8_t> result(SHA256_DIGEST_LENGTH);
    for (std::size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        result[i] = password_sha[i] ^ salted[i];

    return result;
}

#else

// No OpenSSL — return empty (authentication will fail)
inline auto hash_native_password(
    std::string_view, std::span<const std::uint8_t>
) -> std::vector<std::uint8_t> { return {}; }

inline auto hash_caching_sha2(
    std::string_view, std::span<const std::uint8_t>
) -> std::vector<std::uint8_t> { return {}; }

#endif

// =============================================================================
// Select hash function by plugin name
// =============================================================================

inline constexpr std::string_view PLUGIN_NATIVE   = "mysql_native_password";
inline constexpr std::string_view PLUGIN_CSHA2P   = "caching_sha2_password";

inline auto hash_password_for_plugin(
    std::string_view plugin_name,
    std::string_view password,
    std::span<const std::uint8_t> scramble
) -> std::vector<std::uint8_t>
{
    if (plugin_name == PLUGIN_CSHA2P)
        return hash_caching_sha2(password, scramble);
    // Default fallback to mysql_native_password
    return hash_native_password(password, scramble);
}

} // namespace cnetmod::mysql::detail
