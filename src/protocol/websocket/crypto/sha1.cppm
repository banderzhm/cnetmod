module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.websocket:sha1;
import std; // SHA-1 interface declarations

namespace cnetmod::ws::detail {
export auto sha1(const void *source, std::size_t length) noexcept
    -> std::array<std::byte, 20>;
export auto sha1(std::span<const std::byte> input) noexcept
    -> std::array<std::byte, 20>;
export auto sha1(std::string_view input) noexcept -> std::array<std::byte, 20>;
} // namespace cnetmod::ws::detail
