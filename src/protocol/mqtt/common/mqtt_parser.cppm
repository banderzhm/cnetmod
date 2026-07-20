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
  control_packet_type type = control_packet_type::connect;
  std::uint8_t flags = 0; // Lower 4-bit flags
  std::string payload;    // All data after remaining length
};

// =============================================================================
// MQTT Incremental Frame Parser
// =============================================================================

/// Incremental frame parser
/// Feed data via feed(), extract complete frames via next()
export class mqtt_parser {
public:
  mqtt_parser();

  /// Feed new data
  void feed(std::string_view data);

  /// Try to extract next complete frame
  /// Returns nullopt if data is incomplete, need to continue feeding
  auto next() -> std::optional<mqtt_frame>;

  /// Reset parser state
  void reset();

  /// Amount of unconsumed data in current buffer
  [[nodiscard]] auto pending() const noexcept -> std::size_t;

private:
  void compact();

  std::string buf_;
  std::size_t pos_ = 0;
};

} // namespace cnetmod::mqtt
