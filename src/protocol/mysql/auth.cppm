export module cnetmod.protocol.mysql:auth;

import std;
import :protocol;

namespace cnetmod::mysql::detail {
auto hash_native_password(std::string_view password,
                          std::span<const std::uint8_t> scramble)
    -> std::vector<std::uint8_t>;
auto hash_caching_sha2(std::string_view password,
                       std::span<const std::uint8_t> scramble)
    -> std::vector<std::uint8_t>;
inline constexpr std::string_view PLUGIN_NATIVE = "mysql_native_password";
inline constexpr std::string_view PLUGIN_CSHA2P = "caching_sha2_password";
auto hash_password_for_plugin(std::string_view plugin_name,
                              std::string_view password,
                              std::span<const std::uint8_t> scramble)
    -> std::vector<std::uint8_t>;
} // namespace cnetmod::mysql::detail