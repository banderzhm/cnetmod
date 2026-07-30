export module cnetmod.protocol.mongodb:wire_protocol;

import std;
import :error;
import :bson_document;

export namespace cnetmod::mongodb {

inline constexpr std::int32_t op_message = 2013;
inline constexpr std::int32_t op_compressed = 2012;
inline constexpr std::uint8_t compressor_noop = 0;
inline constexpr std::uint8_t compressor_zlib = 2;
inline constexpr std::uint32_t op_message_checksum_present = 1u;
inline constexpr std::uint32_t op_message_more_to_come = 2u;

struct message_header
{
    std::int32_t message_length = 0;
    std::int32_t request_id = 0;
    std::int32_t response_to = 0;
    std::int32_t operation_code = 0;
};

struct decoded_message
{
    message_header header;
    std::uint32_t flags = 0;
    bson_document body;
};

auto encode_command_message(std::int32_t request_id,
    const bson_document& command, std::size_t max_message_bytes)
    -> result<std::vector<std::byte>>;
auto decode_command_message(std::span<const std::byte> bytes,
    std::size_t max_message_bytes, bson_limits limits = {})
    -> result<decoded_message>;
auto decode_message_header(std::span<const std::byte, 16> bytes)
    -> result<message_header>;
auto encode_compressed_message(std::span<const std::byte> message,
    std::uint8_t compressor_id, std::size_t max_message_bytes)
    -> result<std::vector<std::byte>>;
auto decode_compressed_message(std::span<const std::byte> message,
    std::size_t max_message_bytes) -> result<std::vector<std::byte>>;

} // namespace cnetmod::mongodb
