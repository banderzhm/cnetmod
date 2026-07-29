module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.protocol_value_codec;
import std;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.request_header;
import cnetmod.protocol.kafka.response_header;

export namespace cnetmod::kafka::protocol {
class encoder
{
public:
    void int8(std::int8_t);
    void int16(std::int16_t);
    void int32(std::int32_t);
    void int64(std::int64_t);
    void boolean(bool);
    void string(std::string_view);
    void nullable_string(const std::optional<std::string>&);
    void raw(std::span<const std::byte>);
    void byte_array(const std::optional<bytes>&);
    void unsigned_varint(std::uint32_t);
    void varint(std::int32_t);
    void varlong(std::int64_t);
    [[nodiscard]] auto take() && -> bytes;

private:
    bytes data_;
};

class decoder
{
public:
    explicit decoder(std::span<const std::byte> input) noexcept;
    auto int8() -> result<std::int8_t>;
    auto int16() -> result<std::int16_t>;
    auto int32() -> result<std::int32_t>;
    auto int64() -> result<std::int64_t>;
    auto boolean() -> result<bool>;
    auto string() -> result<std::string>;
    auto nullable_string() -> result<std::optional<std::string>>;
    auto byte_array() -> result<std::optional<bytes>>;
    auto unsigned_varint() -> result<std::uint32_t>;
    auto varint() -> result<std::int32_t>;
    auto varlong() -> result<std::int64_t>;
    auto slice(std::size_t) -> result<std::span<const std::byte>>;
    [[nodiscard]] auto remaining() const noexcept -> std::size_t;

private:
    std::span<const std::byte> input_;
    std::size_t pos_ = 0;
};

[[nodiscard]] auto encode_request(request_header,
    std::span<const std::byte> body) -> bytes;
[[nodiscard]] auto decode_response_header(decoder&) -> result<response_header>;
[[nodiscard]] auto crc32c(std::span<const std::byte>) noexcept -> std::uint32_t;
} // namespace cnetmod::kafka::protocol
