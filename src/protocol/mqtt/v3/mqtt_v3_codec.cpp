module cnetmod.protocol.mqtt;

import :codec;
import :v3_codec;

namespace cnetmod::mqtt::v3 {

auto encode(const connect_packet &packet) -> std::string {
  return cnetmod::mqtt::encode_connect(packet.options);
}

auto encode(const publish_packet &packet) -> std::string {
  return cnetmod::mqtt::encode_publish(
      packet.topic, packet.payload, packet.qos_value, packet.retain,
      packet.duplicate, packet.packet_id, protocol_version::v3_1_1);
}

auto encode(const subscribe_packet &packet) -> std::string {
  return cnetmod::mqtt::encode_subscribe(packet.packet_id, packet.entries,
                                         protocol_version::v3_1_1);
}

auto decode_publish(std::string_view payload, std::uint8_t flags)
    -> std::expected<publish_message, std::string> {
  return cnetmod::mqtt::decode_publish(payload, flags,
                                       protocol_version::v3_1_1);
}

auto decode_connack(std::string_view payload)
    -> std::expected<connack_result, std::string> {
  return cnetmod::mqtt::decode_connack(payload, protocol_version::v3_1_1);
}

} // namespace cnetmod::mqtt::v3
