/**
 * @file hmac_sha256.cppm
 * @brief HMAC-SHA256 primitive — wraps OpenSSL HMAC(EVP_sha256())
 *
 * Provides a standalone, reusable HMAC-SHA256 implementation that replaces
 * the inline OpenSSL HMAC() calls scattered across protocol modules
 * (AMQP 1.0 SASL, PostgreSQL SCRAM, MongoDB SCRAM-SHA-256, etc.).
 *
 * Usage:
 *   import cnetmod.utils.hmac_sha256;
 *
 *   auto digest = cnetmod::utils::hmac_sha256("secret-key", "data-to-sign");
 *   auto hex    = cnetmod::utils::hmac_sha256_hex("key", "payload");
 *   auto b64    = cnetmod::utils::hmac_sha256_base64("key", "payload");
 */
export module cnetmod.utils.hmac_sha256;

import std;

export namespace cnetmod::utils {

/// Raw 32-byte HMAC-SHA256 digest
using hmac_sha256_digest = std::array<std::byte, 32>;

/// Compute HMAC-SHA256 over raw byte spans
[[nodiscard]] auto hmac_sha256(std::span<const std::byte> key,
                                std::span<const std::byte> data) -> hmac_sha256_digest;

/// Compute HMAC-SHA256 over string views (convenience overload)
[[nodiscard]] auto hmac_sha256(std::string_view key,
                                std::string_view data) -> hmac_sha256_digest;

/// Compute HMAC-SHA256 and return as lowercase hex string (64 chars)
[[nodiscard]] auto hmac_sha256_hex(std::string_view key,
                                    std::string_view data) -> std::string;

/// Compute HMAC-SHA256 and return as standard Base64 string
[[nodiscard]] auto hmac_sha256_base64(std::string_view key,
                                       std::string_view data) -> std::string;

} // namespace cnetmod::utils
