/// cnetmod.protocol.mqtt:codec — MQTT packet codec interface.
module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:codec;

import std;
import :types;

export namespace cnetmod::mqtt {
namespace detail {
void encode_property(std::string &buffer, const mqtt_property &property);
void encode_properties(std::string &buffer,
                       const properties &properties_to_encode);
auto decode_properties(std::string_view data)
    -> std::pair<properties, std::size_t>;
auto build_packet(std::uint8_t fixed_header, std::string_view payload,
                  std::size_t max_packet_size = 0) -> std::string;

constexpr auto variable_length_size(std::size_t value) noexcept -> std::size_t {
  std::size_t bytes = 1;
  while (value >= 128 && bytes < 4) {
    value >>= 7;
    ++bytes;
  }
  return bytes;
}

void append_packet(std::string &output, std::uint8_t fixed_header,
                   std::string_view payload);
} // namespace detail

struct connack_result {
  bool session_present = false;
  connect_return_code return_code = connect_return_code::accepted;
  std::uint8_t v5_reason = 0;
  properties props;
};

struct ack_result {
  std::uint16_t packet_id = 0;
  std::uint8_t reason_code = 0;
  properties props;
};

struct suback_result {
  std::uint16_t packet_id = 0;
  std::vector<std::uint8_t> return_codes;
  properties props;
};

struct unsuback_result {
  std::uint16_t packet_id = 0;
  std::vector<std::uint8_t> reason_codes;
  properties props;
};

struct disconnect_result {
  std::uint8_t reason_code = 0;
  properties props;
};

struct auth_result {
  std::uint8_t reason_code = 0;
  properties props;
};

struct connect_data {
  protocol_version version = protocol_version::v3_1_1;
  std::string client_id;
  bool clean_session = true;
  std::uint16_t keep_alive = 0;
  std::string username;
  std::string password;
  std::optional<will> will_msg;
  properties props;
  properties will_props;
};

struct subscribe_data {
  std::uint16_t packet_id = 0;
  std::vector<subscribe_entry> entries;
  properties props;
};

struct unsubscribe_data {
  std::uint16_t packet_id = 0;
  std::vector<std::string> topic_filters;
  properties props;
};

auto encode_connect(const connect_options &options) -> std::string;
auto decode_connack(std::string_view payload, protocol_version version)
    -> std::expected<connack_result, std::string>;
auto encode_publish(std::string_view topic, std::string_view payload,
                    qos quality_of_service, bool retain, bool duplicate,
                    std::uint16_t packet_id, protocol_version version,
                    const properties &properties_to_encode = {}) -> std::string;
auto encoded_publish_size(std::string_view topic, std::string_view payload,
                          qos quality_of_service, protocol_version version,
                          const properties &properties_to_encode = {})
    -> std::size_t;
void append_publish(std::string &output, std::string_view topic,
                    std::string_view payload, qos quality_of_service,
                    bool retain, bool duplicate, std::uint16_t packet_id,
                    protocol_version version,
                    const properties &properties_to_encode = {});
auto decode_publish(std::string_view payload, std::uint8_t flags,
                    protocol_version version)
    -> std::expected<publish_message, std::string>;
auto encode_puback(std::uint16_t packet_id, protocol_version version,
                   std::uint8_t reason_code = 0,
                   const properties &properties_to_encode = {}) -> std::string;
auto encode_pubrec(std::uint16_t packet_id, protocol_version version,
                   std::uint8_t reason_code = 0,
                   const properties &properties_to_encode = {}) -> std::string;
auto encode_pubrel(std::uint16_t packet_id, protocol_version version,
                   std::uint8_t reason_code = 0,
                   const properties &properties_to_encode = {}) -> std::string;
auto encode_pubcomp(std::uint16_t packet_id, protocol_version version,
                    std::uint8_t reason_code = 0,
                    const properties &properties_to_encode = {}) -> std::string;
auto decode_ack(std::string_view payload, protocol_version version)
    -> std::expected<ack_result, std::string>;
auto encode_subscribe(std::uint16_t packet_id,
                      const std::vector<subscribe_entry> &entries,
                      protocol_version version,
                      const properties &properties_to_encode = {})
    -> std::string;
auto decode_suback(std::string_view payload, protocol_version version)
    -> std::expected<suback_result, std::string>;
auto encode_unsubscribe(std::uint16_t packet_id,
                        const std::vector<std::string> &topic_filters,
                        protocol_version version,
                        const properties &properties_to_encode = {})
    -> std::string;
auto decode_unsuback(std::string_view payload, protocol_version version)
    -> std::expected<unsuback_result, std::string>;
auto encode_pingreq() -> std::string;
auto encode_pingresp() -> std::string;
auto encode_disconnect(protocol_version version, std::uint8_t reason_code = 0,
                       const properties &properties_to_encode = {})
    -> std::string;
auto decode_disconnect(std::string_view payload, protocol_version version)
    -> disconnect_result;
auto encode_auth(std::uint8_t reason_code = 0,
                 const properties &properties_to_encode = {}) -> std::string;
auto decode_auth(std::string_view payload) -> auth_result;
auto decode_connect(std::string_view payload)
    -> std::expected<connect_data, std::string>;
auto encode_connack(bool session_present, std::uint8_t return_code,
                    protocol_version version,
                    const properties &properties_to_encode = {}) -> std::string;
auto decode_subscribe(std::string_view payload, protocol_version version)
    -> std::expected<subscribe_data, std::string>;
auto encode_suback(std::uint16_t packet_id,
                   const std::vector<std::uint8_t> &return_codes,
                   protocol_version version,
                   const properties &properties_to_encode = {}) -> std::string;
auto decode_unsubscribe(std::string_view payload, protocol_version version)
    -> std::expected<unsubscribe_data, std::string>;
auto encode_unsuback(std::uint16_t packet_id, protocol_version version,
                     const std::vector<std::uint8_t> &reason_codes = {},
                     const properties &properties_to_encode = {})
    -> std::string;
} // namespace cnetmod::mqtt
