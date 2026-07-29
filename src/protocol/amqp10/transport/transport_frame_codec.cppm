module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp10:transport_frame_codec;

import std;
import :primitive_value;
import :protocol_error;

export namespace cnetmod::amqp10 {

enum class frame_type : std::uint8_t
{
    amqp = 0,
    sasl = 1
};
enum class protocol_id : std::uint8_t
{
    amqp = 0,
    sasl = 3
};

struct frame
{
    frame_type type = frame_type::amqp;
    std::uint16_t channel = 0;
    binary body;
};

struct protocol_header
{
    protocol_id protocol = protocol_id::amqp;
    std::uint8_t major = 1;
    std::uint8_t minor = 0;
    std::uint8_t revision = 0;
};

[[nodiscard]] auto encode_protocol_header(protocol_header header) -> binary;
[[nodiscard]] auto decode_protocol_header(std::span<const std::byte> bytes)
    -> std::expected<protocol_header, std::error_code>;
[[nodiscard]] auto encode_frame(const frame& frame) -> binary;
[[nodiscard]] auto decode_frame(std::span<const std::byte> bytes,
    std::uint32_t maximum_size)
    -> std::expected<frame, std::error_code>;

} // namespace cnetmod::amqp10
