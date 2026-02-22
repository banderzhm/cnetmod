/// cnetmod.protocol.mqtt:parser — MQTT Incremental Frame Parser
/// Extracts complete MQTT packet frames from byte stream

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:parser;

import std;
import cnetmod.core.log;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// MQTT Frame
// =============================================================================

/// A complete parsed MQTT frame
export struct mqtt_frame {
    control_packet_type type  = control_packet_type::connect;
    std::uint8_t        flags = 0;       // Lower 4-bit flags
    std::string         payload;         // All data after remaining length
};

// =============================================================================
// MQTT Incremental Frame Parser
// =============================================================================

/// Incremental frame parser
/// Feed data via feed(), extract complete frames via next()
export class mqtt_parser {
public:
    mqtt_parser() = default;

    /// Feed new data
    void feed(std::string_view data) {
        buf_.append(data);
    }

    /// Try to extract next complete frame
    /// Returns nullopt if data is incomplete, need to continue feeding
    auto next() -> std::optional<mqtt_frame> {
        // Need at least 2 bytes: fixed header + 1 byte remaining length
        if (buf_.size() - pos_ < 2) return std::nullopt;

        // Parse fixed header
        auto first_byte = static_cast<std::uint8_t>(buf_[pos_]);
        auto pkt_type = get_packet_type(first_byte);
        auto flags = get_packet_flags(first_byte);

        // Parse remaining length (variable length encoding, max 4 bytes)
        auto [rem_len, vl_bytes] = detail::decode_variable_length(
            std::string_view(buf_).substr(pos_ + 1));

        if (vl_bytes == 0) {
            // Data may be incomplete or invalid
            // Check if there's enough data to determine variable length encoding completion
            auto avail = buf_.size() - pos_ - 1;
            if (avail < 4) {
                // Not enough data, continue waiting
                return std::nullopt;
            }
            // Still can't decode with 4 bytes → invalid
            // Skip this byte to try recovery
            logger::debug("mqtt parser: invalid remaining length, skipping byte");
            ++pos_;
            return std::nullopt;
        }

        std::size_t total_len = 1 + vl_bytes + rem_len;
        if (buf_.size() - pos_ < total_len) {
            // Data incomplete
            return std::nullopt;
        }

        // Complete frame
        mqtt_frame frame;
        frame.type  = pkt_type;
        frame.flags = flags;
        frame.payload = buf_.substr(pos_ + 1 + vl_bytes, rem_len);

        pos_ += total_len;

        // Periodically compact buffer
        compact();

        return frame;
    }

    /// Reset parser state
    void reset() {
        buf_.clear();
        pos_ = 0;
    }

    /// Amount of unconsumed data in current buffer
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
