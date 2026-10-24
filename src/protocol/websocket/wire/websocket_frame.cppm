module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:frame;

import std; // frame API declarations
import :types;

namespace cnetmod::ws {

namespace detail {
    template <bool CheckAscii>
    auto apply_mask_impl(std::span<std::byte> data, std::uint32_t key) noexcept
        -> bool
    {
        const auto wide_key = static_cast<std::uint64_t>(key) |
            (static_cast<std::uint64_t>(key) << 32);
        std::uint64_t high_bits{};
        while (data.size() >= 8)
        {
            std::uint64_t value;
            std::memcpy(&value, data.data(), sizeof(value));
            value ^= wide_key;
            if constexpr (CheckAscii)
                high_bits |= value;
            std::memcpy(data.data(), &value, sizeof(value));
            data = data.subspan(8);
        }
        if (data.size() >= 4)
        {
            std::uint32_t value;
            std::memcpy(&value, data.data(), sizeof(value));
            value ^= key;
            if constexpr (CheckAscii)
                high_bits |= value;
            std::memcpy(data.data(), &value, sizeof(value));
            data = data.subspan(4);
        }
        const auto mask = reinterpret_cast<const std::byte*>(&key);
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            data[i] ^= mask[i & 3];
            if constexpr (CheckAscii)
                high_bits |= std::to_integer<std::uint8_t>(data[i]);
        }
        if constexpr (CheckAscii)
            return (high_bits & 0x8080808080808080ULL) == 0;
        return false;
    }
} // namespace detail

export auto parse_frame_header(std::span<const std::byte> data)
    -> std::expected<std::pair<frame_header, std::size_t>, std::error_code>;
export void apply_mask(std::span<std::byte> data, std::uint32_t key) noexcept;
export void build_frame_into(std::vector<std::byte>& frame, opcode op,
    std::span<const std::byte> payload, bool mask,
    bool fin = true);
export void append_frame_into(std::vector<std::byte>& frames, opcode op,
    std::span<const std::byte> payload, bool mask, bool fin = true);
export auto build_frame(opcode op, std::span<const std::byte> payload,
    bool mask, bool fin = true) -> std::vector<std::byte>;
export auto build_close_frame(std::uint16_t code, std::string_view reason,
    bool mask) -> std::vector<std::byte>;

} // namespace cnetmod::ws
