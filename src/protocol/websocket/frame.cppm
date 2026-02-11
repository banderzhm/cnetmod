module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.websocket:frame;

import std;
import :types;

namespace cnetmod::ws {

// =============================================================================
// 帧头常量 (RFC 6455 §5.2)
// =============================================================================

namespace detail {

inline constexpr std::uint8_t BHB0_FIN     = 0x80;
inline constexpr std::uint8_t BHB0_RSV1    = 0x40;
inline constexpr std::uint8_t BHB0_RSV2    = 0x20;
inline constexpr std::uint8_t BHB0_RSV3    = 0x10;
inline constexpr std::uint8_t BHB0_OPCODE  = 0x0F;
inline constexpr std::uint8_t BHB1_MASK    = 0x80;
inline constexpr std::uint8_t BHB1_PAYLOAD = 0x7F;

} // namespace detail

// =============================================================================
// 解析帧头
// =============================================================================

/// 解析 WebSocket 帧头
/// 返回 (frame_header, header_size)，header_size 是帧头占用的字节数
/// 如果数据不足返回 ws_errc::need_more_data
export auto parse_frame_header(std::span<const std::byte> data)
    -> std::expected<std::pair<frame_header, std::size_t>, std::error_code>
{
    if (data.size() < 2)
        return std::unexpected(make_error_code(ws_errc::need_more_data));

    frame_header hdr;
    auto b0 = static_cast<std::uint8_t>(data[0]);
    auto b1 = static_cast<std::uint8_t>(data[1]);

    hdr.fin  = (b0 & detail::BHB0_FIN)  != 0;
    hdr.rsv1 = (b0 & detail::BHB0_RSV1) != 0;
    hdr.rsv2 = (b0 & detail::BHB0_RSV2) != 0;
    hdr.rsv3 = (b0 & detail::BHB0_RSV3) != 0;
    hdr.op   = static_cast<opcode>(b0 & detail::BHB0_OPCODE);
    hdr.masked = (b1 & detail::BHB1_MASK) != 0;

    std::uint8_t basic_size = b1 & detail::BHB1_PAYLOAD;
    std::size_t header_size = 2;

    if (basic_size <= 125) {
        hdr.payload_length = basic_size;
    } else if (basic_size == 126) {
        if (data.size() < 4)
            return std::unexpected(make_error_code(ws_errc::need_more_data));
        std::uint16_t len16;
        std::memcpy(&len16, data.data() + 2, 2);
        // 网络字节序 -> 主机字节序
        hdr.payload_length = (static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[2])) << 8)
                           | static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[3]));
        header_size = 4;
    } else { // 127
        if (data.size() < 10)
            return std::unexpected(make_error_code(ws_errc::need_more_data));
        hdr.payload_length = 0;
        for (int i = 0; i < 8; ++i) {
            hdr.payload_length = (hdr.payload_length << 8)
                | static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[2 + i]));
        }
        header_size = 10;
    }

    if (hdr.masked) {
        if (data.size() < header_size + 4)
            return std::unexpected(make_error_code(ws_errc::need_more_data));
        std::memcpy(&hdr.masking_key, data.data() + header_size, 4);
        header_size += 4;
    }

    // 验证
    if (is_control(hdr.op)) {
        if (hdr.payload_length > 125)
            return std::unexpected(make_error_code(ws_errc::control_frame_too_large));
        if (!hdr.fin)
            return std::unexpected(make_error_code(ws_errc::fragmented_control));
    }

    return std::pair{hdr, header_size};
}

// =============================================================================
// XOR masking
// =============================================================================

/// 对数据应用/取消 XOR masking (RFC 6455 §5.3)
export void apply_mask(std::span<std::byte> data, std::uint32_t key) noexcept {
    auto mask = reinterpret_cast<const std::byte*>(&key);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] ^= mask[i & 3];
    }
}

// =============================================================================
// 构建帧
// =============================================================================

/// 构建 WebSocket 帧（头 + payload）
/// mask=true 时自动生成随机 masking key 并 mask payload
export auto build_frame(opcode op, std::span<const std::byte> payload,
                        bool mask, bool fin = true) -> std::vector<std::byte>
{
    std::size_t header_size = 2;
    std::uint8_t basic_size;

    if (payload.size() <= 125) {
        basic_size = static_cast<std::uint8_t>(payload.size());
    } else if (payload.size() <= 0xFFFF) {
        basic_size = 126;
        header_size += 2;
    } else {
        basic_size = 127;
        header_size += 8;
    }

    if (mask) header_size += 4;

    std::vector<std::byte> frame(header_size + payload.size());

    // Byte 0: FIN + opcode
    std::uint8_t b0 = static_cast<std::uint8_t>(op);
    if (fin) b0 |= detail::BHB0_FIN;
    frame[0] = static_cast<std::byte>(b0);

    // Byte 1: MASK + payload length
    std::uint8_t b1 = basic_size;
    if (mask) b1 |= detail::BHB1_MASK;
    frame[1] = static_cast<std::byte>(b1);

    std::size_t offset = 2;

    // Extended payload length
    if (basic_size == 126) {
        auto len = static_cast<std::uint16_t>(payload.size());
        frame[offset]     = static_cast<std::byte>((len >> 8) & 0xFF);
        frame[offset + 1] = static_cast<std::byte>(len & 0xFF);
        offset += 2;
    } else if (basic_size == 127) {
        auto len = static_cast<std::uint64_t>(payload.size());
        for (int i = 7; i >= 0; --i) {
            frame[offset + (7 - i)] = static_cast<std::byte>((len >> (i * 8)) & 0xFF);
        }
        offset += 8;
    }

    // Masking key + payload
    if (mask) {
        // 使用简单的伪随机（生产环境建议用 CSPRNG）
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uint32_t key = rng();
        std::memcpy(frame.data() + offset, &key, 4);
        offset += 4;

        // 复制并 mask payload
        std::memcpy(frame.data() + offset, payload.data(), payload.size());
        apply_mask(std::span{frame.data() + offset, payload.size()}, key);
    } else {
        std::memcpy(frame.data() + offset, payload.data(), payload.size());
    }

    return frame;
}

/// 构建 close 帧
export auto build_close_frame(std::uint16_t code, std::string_view reason,
                              bool mask) -> std::vector<std::byte>
{
    std::vector<std::byte> payload;
    payload.reserve(2 + reason.size());

    // Close code (big-endian)
    payload.push_back(static_cast<std::byte>((code >> 8) & 0xFF));
    payload.push_back(static_cast<std::byte>(code & 0xFF));

    // Reason
    for (auto c : reason)
        payload.push_back(static_cast<std::byte>(c));

    return build_frame(opcode::close, payload, mask);
}

} // namespace cnetmod::ws
