module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp10:amqp_value_codec;

import std;
import :primitive_value;
import :protocol_error;

export namespace cnetmod::amqp10 {

class encoder
{
public:
    encoder();
    ~encoder();
    encoder(encoder&&) noexcept;
    auto operator=(encoder&&) noexcept -> encoder&;
    encoder(const encoder&) = delete;
    auto operator=(const encoder&) -> encoder& = delete;

    void write_u8(std::uint8_t value);
    void write_u16(std::uint16_t value);
    void write_u32(std::uint32_t value);
    void write_u64(std::uint64_t value);
    void write_bytes(std::span<const std::byte> bytes);
    void write_value(const value& value);
    [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte>;
    [[nodiscard]] auto release() -> binary;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

class decoder
{
public:
    explicit decoder(std::span<const std::byte> input) noexcept;
    ~decoder();
    decoder(decoder&&) noexcept;
    auto operator=(decoder&&) noexcept -> decoder&;
    decoder(const decoder&) = delete;
    auto operator=(const decoder&) -> decoder& = delete;

    [[nodiscard]] auto read_u8() -> std::expected<std::uint8_t, std::error_code>;
    [[nodiscard]] auto read_u16()
        -> std::expected<std::uint16_t, std::error_code>;
    [[nodiscard]] auto read_u32()
        -> std::expected<std::uint32_t, std::error_code>;
    [[nodiscard]] auto read_u64()
        -> std::expected<std::uint64_t, std::error_code>;
    [[nodiscard]] auto read_bytes(std::size_t count)
        -> std::expected<std::span<const std::byte>, std::error_code>;
    [[nodiscard]] auto read_value() -> std::expected<value, std::error_code>;
    [[nodiscard]] auto remaining() const noexcept -> std::size_t;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace cnetmod::amqp10
