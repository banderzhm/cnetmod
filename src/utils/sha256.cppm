export module cnetmod.utils.sha256;

import std;

export namespace cnetmod::utils {

using sha256_digest = std::array<std::byte, 32>;

[[nodiscard]] auto sha256(std::span<const std::byte> input) -> sha256_digest;
[[nodiscard]] auto sha256(std::string_view input) -> sha256_digest;
[[nodiscard]] auto sha256_hex(std::span<const std::byte> input) -> std::string;
[[nodiscard]] auto sha256_hex(std::string_view input) -> std::string;

} // namespace cnetmod::utils
