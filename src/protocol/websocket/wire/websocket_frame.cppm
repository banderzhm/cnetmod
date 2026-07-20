module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:frame;

import std; // frame API declarations
import :types;

namespace cnetmod::ws {

export auto parse_frame_header(std::span<const std::byte> data)
    -> std::expected<std::pair<frame_header, std::size_t>, std::error_code>;
export void apply_mask(std::span<std::byte> data, std::uint32_t key) noexcept;
export void build_frame_into(std::vector<std::byte> &frame, opcode op,
                             std::span<const std::byte> payload, bool mask,
                             bool fin = true);
export auto build_frame(opcode op, std::span<const std::byte> payload,
                        bool mask, bool fin = true) -> std::vector<std::byte>;
export auto build_close_frame(std::uint16_t code, std::string_view reason,
                              bool mask) -> std::vector<std::byte>;

} // namespace cnetmod::ws
