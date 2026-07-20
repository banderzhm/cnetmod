/// MQTT 5.0 wire codec facade.
export module cnetmod.protocol.mqtt:v5_codec;

import std;
import :types;
import :codec;
import :v5_packet;

export namespace cnetmod::mqtt::v5 {

[[nodiscard]] auto encode(const connect_packet &packet) -> std::string;
[[nodiscard]] auto encode(const publish_packet &packet) -> std::string;
[[nodiscard]] auto encode(const subscribe_packet &packet) -> std::string;
[[nodiscard]] auto decode_publish(std::string_view payload, std::uint8_t flags)
    -> std::expected<publish_message, std::string>;
[[nodiscard]] auto decode_connack(std::string_view payload)
    -> std::expected<connack_result, std::string>;

} // namespace cnetmod::mqtt::v5
