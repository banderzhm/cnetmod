/// MQTT 5.0 packet contracts.  Properties are explicit at this boundary so
/// version-specific extensions never leak into MQTT 3.1.1 callers.
export module cnetmod.protocol.mqtt:v5_packet;

import std;
import :types;

export namespace cnetmod::mqtt::v5 {
struct connect_packet {
  connect_options options;

  explicit connect_packet(connect_options value);
};

struct publish_packet {
  std::string topic;
  std::string payload;
  qos qos_value = qos::at_most_once;
  bool retain = false;
  bool duplicate = false;
  std::uint16_t packet_id = 0;
  properties props;
};

struct subscribe_packet {
  std::uint16_t packet_id = 0;
  std::vector<subscribe_entry> entries;
  properties props;
};
} // namespace cnetmod::mqtt::v5
