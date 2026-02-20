/// cnetmod.protocol.mqtt:parser — MQTT 增量帧解析器
/// 从字节流中提取完整的 MQTT 报文帧

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:parser;

import std;
import cnetmod.core.log;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// MQTT 帧
// =============================================================================

/// 解析出的一个完整 MQTT 帧
export struct mqtt_frame {
    control_packet_type type  = control_packet_type::connect;
    std::uint8_t        flags = 0;       // 低 4 位标志
    std::string         payload;         // remaining length 之后的全部数据
};

// =============================================================================
// MQTT 增量帧解析器
// =============================================================================

/// 增量帧解析器
/// 通过 feed() 喂入数据，next() 取出完整帧
export class mqtt_parser {
public:
    mqtt_parser() = default;

    /// 喂入新数据
    void feed(std::string_view data) {
        buf_.append(data);
    }

    /// 尝试取出下一个完整帧
    /// 返回 nullopt 表示数据不完整，需要继续 feed
    auto next() -> std::optional<mqtt_frame> {
        // 至少需要 2 字节: fixed header + 1 byte remaining length
        if (buf_.size() - pos_ < 2) return std::nullopt;

        // 解析 fixed header
        auto first_byte = static_cast<std::uint8_t>(buf_[pos_]);
        auto pkt_type = get_packet_type(first_byte);
        auto flags = get_packet_flags(first_byte);

        // 解析 remaining length (变长编码，最多 4 字节)
        auto [rem_len, vl_bytes] = detail::decode_variable_length(
            std::string_view(buf_).substr(pos_ + 1));

        if (vl_bytes == 0) {
            // 可能数据不完整，或者无效
            // 检查是否已有足够数据来确定变长编码完成
            auto avail = buf_.size() - pos_ - 1;
            if (avail < 4) {
                // 数据不够，继续等待
                return std::nullopt;
            }
            // 4 字节都有了还解不出来 → 无效
            // 跳过这个字节尝试恢复
            logger::debug("mqtt parser: invalid remaining length, skipping byte");
            ++pos_;
            return std::nullopt;
        }

        std::size_t total_len = 1 + vl_bytes + rem_len;
        if (buf_.size() - pos_ < total_len) {
            // 数据不完整
            return std::nullopt;
        }

        // 完整帧
        mqtt_frame frame;
        frame.type  = pkt_type;
        frame.flags = flags;
        frame.payload = buf_.substr(pos_ + 1 + vl_bytes, rem_len);

        pos_ += total_len;

        // 定期压缩缓冲区
        compact();

        return frame;
    }

    /// 重置解析器状态
    void reset() {
        buf_.clear();
        pos_ = 0;
    }

    /// 当前缓冲区中未消耗的数据量
    [[nodiscard]] auto pending() const noexcept -> std::size_t {
        return buf_.size() - pos_;
    }

private:
    void compact() {
        if (pos_ > 8192) {
            buf_.erase(0, pos_);
            pos_ = 0;
        }
    }

    std::string buf_;
    std::size_t pos_ = 0;
};

} // namespace cnetmod::mqtt
