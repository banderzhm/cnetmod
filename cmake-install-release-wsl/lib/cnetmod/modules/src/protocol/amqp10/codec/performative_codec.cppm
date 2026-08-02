module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:performative_codec;
import std;
import :primitive_value;
import :performative_model;

export namespace cnetmod::amqp10 {
[[nodiscard]] auto encode_performative(const performative& value) -> binary;
[[nodiscard]] auto decode_performative(std::span<const std::byte> bytes)
    -> std::expected<performative, std::error_code>;
} // namespace cnetmod::amqp10
