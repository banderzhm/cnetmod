export module cnetmod.protocol.mongodb:wire_protocol;

import std;
import :error;
import :bson_document;

export namespace cnetmod::mongodb {

inline constexpr std::int32_t op_message = 2013;
inline constexpr std::int32_t op_compressed = 2012;
inline constexpr std::uint8_t compressor_noop = 0;
inline constexpr std::uint8_t compressor_snappy = 1;
inline constexpr std::string_view compressor_snappy_name{"snappy"};
inline constexpr std::uint8_t compressor_zlib = 2;
inline constexpr std::uint8_t compressor_zstd = 3;
inline constexpr std::string_view compressor_zlib_name{"zlib"};
inline constexpr std::string_view compressor_zstd_name{"zstd"};
inline constexpr std::uint32_t op_message_checksum_present = 1u;
inline constexpr std::uint32_t op_message_more_to_come = 2u;
/// Client-to-server capability bit. It permits a server to return an exhaust
/// cursor as consecutive OP_MSG replies marked `moreToCome`.
inline constexpr std::uint32_t op_message_exhaust_allowed = 1u << 16U;

struct message_header
{
    std::int32_t message_length = 0;
    std::int32_t request_id = 0;
    std::int32_t response_to = 0;
    std::int32_t operation_code = 0;
};

/// A Kind 1 section. Each BSON object has its own size limit; the complete
/// sequence is bounded by maxMessageSizeBytes rather than maxBsonObjectSize.
struct document_sequence
{
    std::string identifier;
    std::vector<bson_document> documents;
};

struct decoded_message
{
    message_header header;
    std::uint32_t flags = 0;
    bson_document body;
};

/// Encodes a request body; checksumPresent appends CRC-32C over the complete
/// header and body. moreToCome requests remain unsupported by this API.
auto encode_command_message(std::int32_t request_id,
    const bson_document& command, std::size_t max_message_bytes,
    std::uint32_t flags = 0)
    -> result<std::vector<std::byte>>;
/// Sequence identifiers replace absent (possibly dotted) fields in the body.
auto encode_command_message(std::int32_t request_id,
    const bson_document& command, std::span<const document_sequence> sequences,
    std::size_t max_message_bytes, std::uint32_t flags = 0,
    bson_limits limits = {}) -> result<std::vector<std::byte>>;
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
