/**/ module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.websocket:base64;
import std; // WebSocket wire codec declarations
namespace cnetmod::ws::detail {
export auto base64_encode(const void *input, std::size_t len) -> std::string;
export auto base64_encode(std::span<const std::byte> input) -> std::string;
export auto base64_decode(std::string_view input) -> std::vector<std::byte>;
} // namespace cnetmod::ws::detail
