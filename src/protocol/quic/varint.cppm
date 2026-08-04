module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:varint;

import std;

namespace cnetmod::quic {

// =============================================================================
// QUIC Variable-Length Integer (RFC 9000 §16)
// =============================================================================

export inline constexpr auto max_varint_value =
    (std::uint64_t{1} << 62) - 1;

/// Returns encoding size (1, 2, 4, or 8 bytes) without encoding
export constexpr auto varint_size(std::uint64_t value) noexcept -> std::uint8_t
{
    if (value < (std::uint64_t{1} << 6))
        return 1;
    if (value < (std::uint64_t{1} << 14))
        return 2;
    if (value < (std::uint64_t{1} << 30))
        return 4;
    return 8;
}

/// Decode a QUIC varint from a byte span.
/// Returns decoded value and bytes consumed.
export [[nodiscard]] auto decode_varint(std::span<const std::byte> data)
    -> std::expected<std::pair<std::uint64_t, std::size_t>, std::error_code>;

/// Encode a QUIC varint into a fixed array.
/// Returns encoded bytes and actual length used.
export [[nodiscard]] auto encode_varint(std::uint64_t value)
    -> std::expected<std::pair<std::array<std::byte, 8>, std::size_t>,
        std::error_code>;

/// Encode a QUIC varint directly into an output buffer.
/// Returns bytes written.
export [[nodiscard]] auto encode_varint_to(
    std::uint64_t value, std::span<std::byte> output)
    -> std::expected<std::size_t, std::error_code>;

} // namespace cnetmod::quic
