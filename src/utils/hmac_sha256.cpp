/**
 * @file hmac_sha256.cpp
 * @brief HMAC-SHA256 implementation — OpenSSL EVP backend
 */
module;

#include <openssl/evp.h>
#include <openssl/hmac.h>

module cnetmod.utils.hmac_sha256;

import std;

namespace cnetmod::utils {
namespace {

    /// Standard Base64 alphabet (RFC 4648 §4)
    constexpr char base64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    auto base64_encode(std::span<const std::byte> input) -> std::string
    {
        std::string out;
        const auto* src = reinterpret_cast<const std::uint8_t*>(input.data());
        const auto len = input.size();
        out.reserve(((len + 2) / 3) * 4);

        for (std::size_t i = 0; i < len; i += 3)
        {
            const auto a = src[i];
            const auto b = (i + 1 < len) ? src[i + 1] : std::uint8_t{0};
            const auto c = (i + 2 < len) ? src[i + 2] : std::uint8_t{0};

            out.push_back(base64_table[a >> 2]);
            out.push_back(base64_table[((a & 0x03) << 4) | (b >> 4)]);
            out.push_back((i + 1 < len) ? base64_table[((b & 0x0F) << 2) | (c >> 6)] : '=');
            out.push_back((i + 2 < len) ? base64_table[c & 0x3F] : '=');
        }
        return out;
    }

    auto compute_hmac(std::span<const std::byte> key,
                      std::span<const std::byte> data) -> hmac_sha256_digest
    {
        hmac_sha256_digest digest{};
        unsigned int len = 0;
        HMAC(EVP_sha256(),
             key.data(), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(),
             reinterpret_cast<unsigned char*>(digest.data()), &len);
        return digest;
    }

} // namespace

auto hmac_sha256(std::span<const std::byte> key,
                  std::span<const std::byte> data) -> hmac_sha256_digest
{
    return compute_hmac(key, data);
}

auto hmac_sha256(std::string_view key,
                  std::string_view data) -> hmac_sha256_digest
{
    return compute_hmac(
        std::as_bytes(std::span(key.data(), key.size())),
        std::as_bytes(std::span(data.data(), data.size())));
}

auto hmac_sha256_hex(std::string_view key,
                      std::string_view data) -> std::string
{
    const auto digest = hmac_sha256(key, data);
    std::string text;
    text.reserve(64);
    for (auto byte : digest)
        text += std::format("{:02x}", std::to_integer<unsigned char>(byte));
    return text;
}

auto hmac_sha256_base64(std::string_view key,
                         std::string_view data) -> std::string
{
    const auto digest = hmac_sha256(key, data);
    return base64_encode(std::span<const std::byte>(digest.data(), digest.size()));
}

} // namespace cnetmod::utils
