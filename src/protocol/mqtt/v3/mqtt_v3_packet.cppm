/// MQTT 3.1.1 packet contracts.
/// These contracts deliberately do not expose MQTT 5 properties: callers that
/// need them must use the v5 protocol partition explicitly.
export module cnetmod.protocol.mqtt:v3_packet;

import std;
import :types;

export namespace cnetmod::mqtt::v3 {

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
};

struct subscribe_packet {
  std::uint16_t packet_id = 0;
  std::vector<subscribe_entry> entries;
};

} // namespace cnetmod::mqtt::v3
