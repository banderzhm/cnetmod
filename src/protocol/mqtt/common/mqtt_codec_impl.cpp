/// MQTT Packet Encoding/Decoding implementation.
module cnetmod.protocol.mqtt;

import std;
import cnetmod.core.log;
import :types;
import :codec;

namespace cnetmod::mqtt {
using detail::decode_variable_length;
using detail::encode_variable_length;
using detail::read_u16;
using detail::read_u32;
using detail::write_binary;
using detail::write_u16;
using detail::write_u32;
using detail::write_utf8_string;

// =============================================================================
// CONNECT Encoding
// =============================================================================

auto encode_connect(const connect_options &opts) -> std::string {
  std::string payload;
  payload.reserve(256);

  bool is_v5 = (opts.version == protocol_version::v5);

  // Variable header: Protocol Name + Protocol Level + Connect Flags + Keep
  // Alive Protocol Name: "MQTT"
  detail::write_utf8_string(payload, "MQTT");

  // Protocol Level
  payload.push_back(static_cast<char>(static_cast<std::uint8_t>(opts.version)));

  // Connect Flags
  std::uint8_t flags = 0;
  if (opts.clean_session)
    flags |= 0x02;
  if (opts.will_msg) {
    flags |= 0x04; // will flag
    flags |= (static_cast<std::uint8_t>(opts.will_msg->qos_value) << 3);
    if (opts.will_msg->retain)
      flags |= 0x20;
  }
  if (!opts.password.empty())
    flags |= 0x40;
  if (!opts.username.empty())
    flags |= 0x80;
  payload.push_back(static_cast<char>(flags));

  // Keep Alive
  detail::write_u16(payload, opts.keep_alive_sec);

  // v5: CONNECT Properties
  if (is_v5) {
    detail::encode_properties(payload, opts.props);
  }

  // Payload: Client Identifier
  detail::write_utf8_string(payload, opts.client_id);

  // Payload: Will Properties + Will Topic + Will Message
  if (opts.will_msg) {
    if (is_v5) {
      detail::encode_properties(payload, opts.will_msg->props);
    }
    detail::write_utf8_string(payload, opts.will_msg->topic);
    detail::write_binary(payload, opts.will_msg->message);
  }

  // Payload: Username
  if (!opts.username.empty()) {
    detail::write_utf8_string(payload, opts.username);
  }

  // Payload: Password
  if (!opts.password.empty()) {
    detail::write_binary(payload, opts.password);
  }

  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::connect) | 0x00, payload);
}

// =============================================================================
// CONNACK Decoding
// =============================================================================

auto decode_connack(std::string_view payload, protocol_version ver)
    -> std::expected<connack_result, std::string> {
  if (payload.size() < 2)
    return std::unexpected("CONNACK too short");

  connack_result r;
  r.session_present = (static_cast<std::uint8_t>(payload[0]) & 0x01) != 0;

  if (ver == protocol_version::v5) {
    r.v5_reason = static_cast<std::uint8_t>(payload[1]);
    if (payload.size() > 2) {
      auto [props, consumed] = detail::decode_properties(payload.substr(2));
      r.props = std::move(props);
    }
  } else {
    r.return_code = static_cast<connect_return_code>(payload[1]);
  }
  return r;
}

// =============================================================================
// PUBLISH Encoding/Decoding
// =============================================================================

auto encode_publish(std::string_view topic, std::string_view payload_data,
                    qos q, bool retain, bool dup, std::uint16_t packet_id,
                    protocol_version ver, const properties &props)
    -> std::string {
  std::string payload;
  payload.reserve(topic.size() + payload_data.size() + 32);

  // Topic Name
  detail::write_utf8_string(payload, topic);

  // Packet Identifier (only QoS 1 or 2)
  if (q != qos::at_most_once) {
    detail::write_u16(payload, packet_id);
  }

  // v5 Properties
  if (ver == protocol_version::v5) {
    detail::encode_properties(payload, props);
  }

  // Payload
  payload.append(payload_data);

  // Fixed header flags
  std::uint8_t fh = static_cast<std::uint8_t>(control_packet_type::publish);
  if (dup)
    fh |= 0x08;
  fh |= (static_cast<std::uint8_t>(q) << 1);
  if (retain)
    fh |= 0x01;

  return detail::build_packet(fh, payload);
}

auto encoded_publish_size(std::string_view topic, std::string_view payload_data,
                          qos q, protocol_version ver, const properties &props)
    -> std::size_t {
  std::size_t remaining = 2 + topic.size() + payload_data.size();
  if (q != qos::at_most_once) {
    remaining += 2;
  }
  if (ver == protocol_version::v5) {
    if (props.empty()) {
      remaining += 1;
    } else {
      std::string prop_buf;
      for (auto &p : props) {
        detail::encode_property(prop_buf, p);
      }
      remaining +=
          detail::variable_length_size(prop_buf.size()) + prop_buf.size();
    }
  }
  return 1 + detail::variable_length_size(remaining) + remaining;
}

void append_publish(std::string &out, std::string_view topic,
                    std::string_view payload_data, qos q, bool retain, bool dup,
                    std::uint16_t packet_id, protocol_version ver,
                    const properties &props) {
  std::string prop_buf;
  std::size_t remaining = 2 + topic.size() + payload_data.size();
  if (q != qos::at_most_once) {
    remaining += 2;
  }
  if (ver == protocol_version::v5) {
    if (!props.empty()) {
      for (auto &p : props) {
        detail::encode_property(prop_buf, p);
      }
    }
    remaining +=
        detail::variable_length_size(prop_buf.size()) + prop_buf.size();
  }

  std::uint8_t fh = static_cast<std::uint8_t>(control_packet_type::publish);
  if (dup)
    fh |= 0x08;
  fh |= (static_cast<std::uint8_t>(q) << 1);
  if (retain)
    fh |= 0x01;

  out.reserve(out.size() + 1 + detail::variable_length_size(remaining) +
              remaining);
  out.push_back(static_cast<char>(fh));
  detail::encode_variable_length(out, remaining);
  detail::write_utf8_string(out, topic);
  if (q != qos::at_most_once) {
    detail::write_u16(out, packet_id);
  }
  if (ver == protocol_version::v5) {
    detail::encode_variable_length(out, prop_buf.size());
    out.append(prop_buf);
  }
  out.append(payload_data);
}

/// Decode PUBLISH packet (from frame payload + flags)
auto decode_publish(std::string_view payload, std::uint8_t flags,
                    protocol_version ver)
    -> std::expected<publish_message, std::string> {
  publish_message msg;
  msg.dup = (flags & 0x08) != 0;
  msg.qos_value = static_cast<qos>((flags >> 1) & 0x03);
  msg.retain = (flags & 0x01) != 0;

  if (payload.size() < 2)
    return std::unexpected("PUBLISH too short");

  // Topic Name
  auto topic_len = detail::read_u16(payload);
  std::size_t pos = 2;
  if (pos + topic_len > payload.size())
    return std::unexpected("PUBLISH topic overflow");
  msg.topic = std::string(payload.substr(pos, topic_len));
  pos += topic_len;

  // MQTT UTF-8 compliance validation
  if (!validate_utf8(msg.topic)) {
    logger::debug("mqtt decode_publish: invalid UTF-8 in topic");
    return std::unexpected("PUBLISH: invalid UTF-8 in topic");
  }

  // Packet Identifier
  if (msg.qos_value != qos::at_most_once) {
    if (pos + 2 > payload.size())
      return std::unexpected("PUBLISH missing packet_id");
    msg.packet_id = detail::read_u16(payload.substr(pos));
    pos += 2;
  }

  // v5 Properties
  if (ver == protocol_version::v5) {
    auto [props, consumed] = detail::decode_properties(payload.substr(pos));
    msg.props = std::move(props);
    pos += consumed;
  }

  // Payload
  msg.payload = std::string(payload.substr(pos));
  return msg;
}

// =============================================================================
// PUBACK / PUBREC / PUBREL / PUBCOMP
// =============================================================================

auto encode_puback(std::uint16_t packet_id, protocol_version ver,
                   std::uint8_t reason_code, const properties &props)
    -> std::string {
  std::string payload;
  detail::write_u16(payload, packet_id);
  if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
    payload.push_back(static_cast<char>(reason_code));
    if (!props.empty())
      detail::encode_properties(payload, props);
  }
  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::puback), payload);
}

auto encode_pubrec(std::uint16_t packet_id, protocol_version ver,
                   std::uint8_t reason_code, const properties &props)
    -> std::string {
  std::string payload;
  detail::write_u16(payload, packet_id);
  if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
    payload.push_back(static_cast<char>(reason_code));
    if (!props.empty())
      detail::encode_properties(payload, props);
  }
  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::pubrec), payload);
}

auto encode_pubrel(std::uint16_t packet_id, protocol_version ver,
                   std::uint8_t reason_code, const properties &props)
    -> std::string {
  std::string payload;
  detail::write_u16(payload, packet_id);
  if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
    payload.push_back(static_cast<char>(reason_code));
    if (!props.empty())
      detail::encode_properties(payload, props);
  }
  // PUBREL fixed header: 0x62 (type 6, flags 0010)
  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::pubrel) | 0x02, payload);
}

auto encode_pubcomp(std::uint16_t packet_id, protocol_version ver,
                    std::uint8_t reason_code, const properties &props)
    -> std::string {
  std::string payload;
  detail::write_u16(payload, packet_id);
  if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
    payload.push_back(static_cast<char>(reason_code));
    if (!props.empty())
      detail::encode_properties(payload, props);
  }
  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::pubcomp), payload);
}

auto decode_ack(std::string_view payload, protocol_version ver)
    -> std::expected<ack_result, std::string> {
  if (payload.size() < 2)
    return std::unexpected("ACK too short");
  ack_result r;
  r.packet_id = detail::read_u16(payload);
  if (ver == protocol_version::v5 && payload.size() > 2) {
    r.reason_code = static_cast<std::uint8_t>(payload[2]);
    if (payload.size() > 3) {
      auto [props, consumed] = detail::decode_properties(payload.substr(3));
      r.props = std::move(props);
    }
  }
  return r;
}

// =============================================================================
// SUBSCRIBE
// =============================================================================

auto encode_subscribe(std::uint16_t packet_id,
                      const std::vector<subscribe_entry> &entries,
                      protocol_version ver, const properties &props)
    -> std::string {
  std::string payload;
  payload.reserve(128);

  // Packet Identifier
  detail::write_u16(payload, packet_id);

  // v5 Properties
  if (ver == protocol_version::v5) {
    detail::encode_properties(payload, props);
  }

  // Subscription entries
  for (auto &e : entries) {
    detail::write_utf8_string(payload, e.topic_filter);
    if (ver == protocol_version::v5) {
      payload.push_back(static_cast<char>(e.encode_options()));
    } else {
      payload.push_back(
          static_cast<char>(static_cast<std::uint8_t>(e.max_qos)));
    }
  }

  // SUBSCRIBE fixed header: 0x82 (type 8, flags 0010)
  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::subscribe) | 0x02,
      payload);
}

// =============================================================================
// SUBACK Decoding
// =============================================================================

auto decode_suback(std::string_view payload, protocol_version ver)
    -> std::expected<suback_result, std::string> {
  if (payload.size() < 2)
    return std::unexpected("SUBACK too short");
  suback_result r;
  r.packet_id = detail::read_u16(payload);
  std::size_t pos = 2;

  if (ver == protocol_version::v5) {
    auto [props, consumed] = detail::decode_properties(payload.substr(pos));
    r.props = std::move(props);
    pos += consumed;
  }

  while (pos < payload.size()) {
    r.return_codes.push_back(static_cast<std::uint8_t>(payload[pos]));
    ++pos;
  }
  return r;
}

// =============================================================================
// UNSUBSCRIBE
// =============================================================================

auto encode_unsubscribe(std::uint16_t packet_id,
                        const std::vector<std::string> &topic_filters,
                        protocol_version ver, const properties &props)
    -> std::string {
  std::string payload;
  payload.reserve(128);

  detail::write_u16(payload, packet_id);

  if (ver == protocol_version::v5) {
    detail::encode_properties(payload, props);
  }

  for (auto &tf : topic_filters) {
    detail::write_utf8_string(payload, tf);
  }

  // UNSUBSCRIBE fixed header: 0xA2 (type 10, flags 0010)
  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::unsubscribe) | 0x02,
      payload);
}

// =============================================================================
// UNSUBACK Decoding
// =============================================================================

auto decode_unsuback(std::string_view payload, protocol_version ver)
    -> std::expected<unsuback_result, std::string> {
  if (payload.size() < 2)
    return std::unexpected("UNSUBACK too short");
  unsuback_result r;
  r.packet_id = detail::read_u16(payload);
  std::size_t pos = 2;

  if (ver == protocol_version::v5) {
    auto [props, consumed] = detail::decode_properties(payload.substr(pos));
    r.props = std::move(props);
    pos += consumed;
    while (pos < payload.size()) {
      r.reason_codes.push_back(static_cast<std::uint8_t>(payload[pos]));
      ++pos;
    }
  }
  return r;
}

// =============================================================================
// PINGREQ / PINGRESP
// =============================================================================

auto encode_pingreq() -> std::string {
  return {static_cast<char>(
              static_cast<std::uint8_t>(control_packet_type::pingreq)),
          static_cast<char>(0)};
}

auto encode_pingresp() -> std::string {
  return {static_cast<char>(
              static_cast<std::uint8_t>(control_packet_type::pingresp)),
          static_cast<char>(0)};
}

// =============================================================================
// DISCONNECT
// =============================================================================

auto encode_disconnect(protocol_version ver, std::uint8_t reason_code,
                       const properties &props) -> std::string {
  if (ver == protocol_version::v5 && (reason_code != 0 || !props.empty())) {
    std::string payload;
    payload.push_back(static_cast<char>(reason_code));
    if (!props.empty()) {
      detail::encode_properties(payload, props);
    } else {
      detail::encode_variable_length(payload, 0);
    }
    return detail::build_packet(
        static_cast<std::uint8_t>(control_packet_type::disconnect), payload);
  }
  // v3.1.1 or v5 normal disconnect with no properties
  return {static_cast<char>(
              static_cast<std::uint8_t>(control_packet_type::disconnect)),
          static_cast<char>(0)};
}

auto decode_disconnect(std::string_view payload, protocol_version ver)
    -> disconnect_result {
  disconnect_result r;
  if (ver == protocol_version::v5 && !payload.empty()) {
    r.reason_code = static_cast<std::uint8_t>(payload[0]);
    if (payload.size() > 1) {
      auto [props, consumed] = detail::decode_properties(payload.substr(1));
      r.props = std::move(props);
    }
  }
  return r;
}

// =============================================================================
// AUTH (v5 only)
// =============================================================================

auto encode_auth(std::uint8_t reason_code, const properties &props)
    -> std::string {
  std::string payload;
  payload.push_back(static_cast<char>(reason_code));
  detail::encode_properties(payload, props);
  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::auth), payload);
}

auto decode_auth(std::string_view payload) -> auth_result {
  auth_result r;
  if (!payload.empty()) {
    r.reason_code = static_cast<std::uint8_t>(payload[0]);
    if (payload.size() > 1) {
      auto [props, consumed] = detail::decode_properties(payload.substr(1));
      r.props = std::move(props);
    }
  }
  return r;
}

// =============================================================================
// ================== Broker-side Encoding/Decoding (Server-side codec)
// ==================
// =============================================================================

// =============================================================================
// CONNECT Decoding (Server receives)
// =============================================================================

auto decode_connect(std::string_view payload)
    -> std::expected<connect_data, std::string> {
  connect_data r;
  if (payload.size() < 10)
    return std::unexpected("CONNECT too short");

  std::size_t pos = 0;

  // Protocol Name (length-prefixed "MQTT")
  if (pos + 2 > payload.size())
    return std::unexpected("CONNECT: no protocol name length");
  auto name_len = detail::read_u16(payload.substr(pos));
  pos += 2;
  if (pos + name_len > payload.size())
    return std::unexpected("CONNECT: protocol name overflow");
  auto proto_name = payload.substr(pos, name_len);
  pos += name_len;
  if (proto_name != "MQTT")
    return std::unexpected("CONNECT: invalid protocol name");

  // Protocol Level
  if (pos >= payload.size())
    return std::unexpected("CONNECT: no protocol level");
  auto level = static_cast<std::uint8_t>(payload[pos]);
  pos++;
  if (level == 4)
    r.version = protocol_version::v3_1_1;
  else if (level == 5)
    r.version = protocol_version::v5;
  else
    return std::unexpected("CONNECT: unsupported protocol level " +
                           std::to_string(level));

  // Connect Flags
  if (pos >= payload.size())
    return std::unexpected("CONNECT: no flags");
  auto flags = static_cast<std::uint8_t>(payload[pos]);
  pos++;
  // MQTT spec requires reserved bit (bit 0) must be 0
  if (flags & 0x01)
    return std::unexpected("CONNECT: reserved flag bit 0 must be 0");
  r.clean_session = (flags & 0x02) != 0;
  bool has_will = (flags & 0x04) != 0;
  auto will_qos = static_cast<qos>((flags >> 3) & 0x03);
  bool will_retain = (flags & 0x20) != 0;
  bool has_password = (flags & 0x40) != 0;
  bool has_username = (flags & 0x80) != 0;

  // Keep Alive
  if (pos + 2 > payload.size())
    return std::unexpected("CONNECT: no keep alive");
  r.keep_alive = detail::read_u16(payload.substr(pos));
  pos += 2;

  // v5: CONNECT Properties
  bool is_v5 = (r.version == protocol_version::v5);
  if (is_v5) {
    auto [props, consumed] = detail::decode_properties(payload.substr(pos));
    r.props = std::move(props);
    pos += consumed;
  }

  // Payload: Client Identifier
  if (pos + 2 > payload.size())
    return std::unexpected("CONNECT: no client_id length");
  auto cid_len = detail::read_u16(payload.substr(pos));
  pos += 2;
  if (pos + cid_len > payload.size())
    return std::unexpected("CONNECT: client_id overflow");
  r.client_id = std::string(payload.substr(pos, cid_len));
  pos += cid_len;

  // MQTT UTF-8 compliance validation
  if (!r.client_id.empty() && !validate_utf8(r.client_id)) {
    logger::debug("mqtt decode_connect: invalid UTF-8 in client_id");
    return std::unexpected("CONNECT: invalid UTF-8 in client_id");
  }

  // Payload: Will
  if (has_will) {
    will w;
    w.qos_value = will_qos;
    w.retain = will_retain;

    // v5: Will Properties
    if (is_v5) {
      auto [wp, wc] = detail::decode_properties(payload.substr(pos));
      r.will_props = std::move(wp);
      w.props = r.will_props;
      pos += wc;
    }

    // Will Topic
    if (pos + 2 > payload.size())
      return std::unexpected("CONNECT: no will topic length");
    auto wt_len = detail::read_u16(payload.substr(pos));
    pos += 2;
    if (pos + wt_len > payload.size())
      return std::unexpected("CONNECT: will topic overflow");
    w.topic = std::string(payload.substr(pos, wt_len));
    pos += wt_len;

    // Will Topic UTF-8 validation
    if (!validate_utf8(w.topic)) {
      logger::debug("mqtt decode_connect: invalid UTF-8 in will topic");
      return std::unexpected("CONNECT: invalid UTF-8 in will topic");
    }

    // Will Payload
    if (pos + 2 > payload.size())
      return std::unexpected("CONNECT: no will payload length");
    auto wp_len = detail::read_u16(payload.substr(pos));
    pos += 2;
    if (pos + wp_len > payload.size())
      return std::unexpected("CONNECT: will payload overflow");
    w.message = std::string(payload.substr(pos, wp_len));
    pos += wp_len;

    r.will_msg = std::move(w);
  }

  // Payload: Username
  if (has_username) {
    if (pos + 2 > payload.size())
      return std::unexpected("CONNECT: no username length");
    auto u_len = detail::read_u16(payload.substr(pos));
    pos += 2;
    if (pos + u_len > payload.size())
      return std::unexpected("CONNECT: username overflow");
    r.username = std::string(payload.substr(pos, u_len));
    pos += u_len;

    if (!validate_utf8(r.username)) {
      logger::debug("mqtt decode_connect: invalid UTF-8 in username");
      return std::unexpected("CONNECT: invalid UTF-8 in username");
    }
  }

  // Payload: Password
  if (has_password) {
    if (pos + 2 > payload.size())
      return std::unexpected("CONNECT: no password length");
    auto p_len = detail::read_u16(payload.substr(pos));
    pos += 2;
    if (pos + p_len > payload.size())
      return std::unexpected("CONNECT: password overflow");
    r.password = std::string(payload.substr(pos, p_len));
    pos += p_len;
  }

  return r;
}

// =============================================================================
// CONNACK Encoding (Server sends)
// =============================================================================

auto encode_connack(bool session_present, std::uint8_t return_code,
                    protocol_version ver, const properties &props)
    -> std::string {
  std::string payload;
  payload.reserve(32);

  // Connect Acknowledge Flags
  payload.push_back(session_present ? static_cast<char>(0x01)
                                    : static_cast<char>(0x00));

  // Return Code / Reason Code
  payload.push_back(static_cast<char>(return_code));

  // v5: CONNACK Properties
  if (ver == protocol_version::v5) {
    detail::encode_properties(payload, props);
  }

  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::connack), payload);
}

// =============================================================================
// SUBSCRIBE Decoding (Server receives)
// =============================================================================

auto decode_subscribe(std::string_view payload, protocol_version ver)
    -> std::expected<subscribe_data, std::string> {
  if (payload.size() < 5)
    return std::unexpected("SUBSCRIBE too short");
  subscribe_data r;
  std::size_t pos = 0;

  // Packet Identifier
  r.packet_id = detail::read_u16(payload.substr(pos));
  pos += 2;

  // v5 Properties
  if (ver == protocol_version::v5) {
    auto [props, consumed] = detail::decode_properties(payload.substr(pos));
    r.props = std::move(props);
    pos += consumed;
  }

  // v5: Extract Subscription Identifier
  std::uint32_t sub_id = 0;
  if (ver == protocol_version::v5) {
    for (auto &p : r.props)
      if (p.id == property_id::subscription_identifier)
        if (auto *v = std::get_if<std::uint32_t>(&p.value))
          sub_id = *v;
  }

  // Subscription entries
  while (pos < payload.size()) {
    if (pos + 2 > payload.size())
      break;
    auto tf_len = detail::read_u16(payload.substr(pos));
    pos += 2;
    if (pos + tf_len > payload.size())
      break;
    std::string topic_filter(payload.substr(pos, tf_len));
    pos += tf_len;

    if (pos >= payload.size())
      break;
    auto opts_byte = static_cast<std::uint8_t>(payload[pos]);
    pos++;

    // MQTT UTF-8 compliance validation
    if (!validate_utf8(topic_filter)) {
      logger::debug("mqtt decode_subscribe: invalid UTF-8 in topic_filter");
      return std::unexpected("SUBSCRIBE: invalid UTF-8 in topic_filter");
    }

    subscribe_entry entry;
    entry.topic_filter = std::move(topic_filter);
    entry.max_qos = static_cast<qos>(opts_byte & 0x03);
    if (ver == protocol_version::v5) {
      entry.no_local = (opts_byte & 0x04) != 0;
      entry.retain_as_published = (opts_byte & 0x08) != 0;
      entry.rh = static_cast<retain_handling>((opts_byte >> 4) & 0x03);
      entry.subscription_id = sub_id;
    }
    r.entries.push_back(std::move(entry));
  }

  return r;
}

// =============================================================================
// SUBACK Encoding (Server sends)
// =============================================================================

auto encode_suback(std::uint16_t packet_id,
                   const std::vector<std::uint8_t> &return_codes,
                   protocol_version ver, const properties &props)
    -> std::string {
  std::string payload;
  payload.reserve(16 + return_codes.size());

  detail::write_u16(payload, packet_id);

  if (ver == protocol_version::v5) {
    detail::encode_properties(payload, props);
  }

  for (auto rc : return_codes) {
    payload.push_back(static_cast<char>(rc));
  }

  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::suback), payload);
}

// =============================================================================
// UNSUBSCRIBE Decoding (Server receives)
// =============================================================================

auto decode_unsubscribe(std::string_view payload, protocol_version ver)
    -> std::expected<unsubscribe_data, std::string> {
  if (payload.size() < 4)
    return std::unexpected("UNSUBSCRIBE too short");
  unsubscribe_data r;
  std::size_t pos = 0;

  // Packet Identifier
  r.packet_id = detail::read_u16(payload.substr(pos));
  pos += 2;

  // v5 Properties
  if (ver == protocol_version::v5) {
    auto [props, consumed] = detail::decode_properties(payload.substr(pos));
    r.props = std::move(props);
    pos += consumed;
  }

  // Topic Filters
  while (pos < payload.size()) {
    if (pos + 2 > payload.size())
      break;
    auto tf_len = detail::read_u16(payload.substr(pos));
    pos += 2;
    if (pos + tf_len > payload.size())
      break;
    r.topic_filters.emplace_back(payload.substr(pos, tf_len));
    pos += tf_len;
  }

  return r;
}

// =============================================================================
// UNSUBACK Encoding (Server sends)
// =============================================================================

auto encode_unsuback(std::uint16_t packet_id, protocol_version ver,
                     const std::vector<std::uint8_t> &reason_codes,
                     const properties &props) -> std::string {
  std::string payload;
  payload.reserve(16 + reason_codes.size());

  detail::write_u16(payload, packet_id);

  if (ver == protocol_version::v5) {
    detail::encode_properties(payload, props);
    for (auto rc : reason_codes) {
      payload.push_back(static_cast<char>(rc));
    }
  }

  return detail::build_packet(
      static_cast<std::uint8_t>(control_packet_type::unsuback), payload);
}
} // namespace cnetmod::mqtt

namespace cnetmod::mqtt::detail {
void encode_property(std::string &buffer, const mqtt_property &property) {
  buffer.push_back(static_cast<char>(static_cast<std::uint8_t>(property.id)));
  std::visit(
      [&](const auto &value) {
        using value_type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<value_type, std::uint8_t>) {
          buffer.push_back(static_cast<char>(value));
        } else if constexpr (std::is_same_v<value_type, std::uint16_t>) {
          write_u16(buffer, value);
        } else if constexpr (std::is_same_v<value_type, std::uint32_t>) {
          if (property.id == property_id::subscription_identifier)
            encode_variable_length(buffer, value);
          else
            write_u32(buffer, value);
        } else if constexpr (std::is_same_v<value_type, std::string>) {
          if (property.id == property_id::correlation_data ||
              property.id == property_id::authentication_data)
            write_binary(buffer, value);
          else
            write_utf8_string(buffer, value);
        } else {
          write_utf8_string(buffer, value.first);
          write_utf8_string(buffer, value.second);
        }
      },
      property.value);
}

void encode_properties(std::string &buffer,
                       const properties &properties_to_encode) {
  std::string property_buffer;
  for (const auto &property : properties_to_encode)
    encode_property(property_buffer, property);
  encode_variable_length(buffer, property_buffer.size());
  buffer.append(property_buffer);
}

auto build_packet(std::uint8_t fixed_header, std::string_view payload,
                  std::size_t max_packet_size) -> std::string {
  std::string packet;
  packet.reserve(1 + 4 + payload.size());
  packet.push_back(static_cast<char>(fixed_header));
  encode_variable_length(packet, payload.size());
  packet.append(payload);
  if (max_packet_size != 0 && packet.size() > max_packet_size)
    return {};
  return packet;
}

void append_packet(std::string &output, std::uint8_t fixed_header,
                   std::string_view payload) {
  output.reserve(output.size() + 1 + variable_length_size(payload.size()) +
                 payload.size());
  output.push_back(static_cast<char>(fixed_header));
  encode_variable_length(output, payload.size());
  output.append(payload);
}

auto decode_properties(std::string_view data)
    -> std::pair<properties, std::size_t> {
  properties decoded;
  if (data.empty())
    return {std::move(decoded), 0};
  const auto [property_length, length_bytes] = decode_variable_length(data);
  if (length_bytes == 0 || property_length > data.size() - length_bytes)
    return {std::move(decoded), 0};

  const auto property_data = data.substr(length_bytes, property_length);
  std::size_t position = 0;
  while (position < property_data.size()) {
    const auto id = static_cast<property_id>(
        static_cast<std::uint8_t>(property_data[position++]));
    mqtt_property property{.id = id};
    const auto read_string = [&]() -> std::optional<std::string> {
      if (position + 2 > property_data.size())
        return std::nullopt;
      const auto length = read_u16(property_data.substr(position));
      position += 2;
      if (position + length > property_data.size())
        return std::nullopt;
      auto result = std::string(property_data.substr(position, length));
      position += length;
      return result;
    };

    switch (id) {
    case property_id::payload_format_indicator:
    case property_id::request_problem_information:
    case property_id::request_response_information:
    case property_id::maximum_qos:
    case property_id::retain_available:
    case property_id::wildcard_subscription_available:
    case property_id::subscription_identifier_available:
    case property_id::shared_subscription_available:
      if (position >= property_data.size())
        return {std::move(decoded), length_bytes + property_length};
      property.value = static_cast<std::uint8_t>(property_data[position++]);
      break;
    case property_id::server_keep_alive:
    case property_id::receive_maximum:
    case property_id::topic_alias_maximum:
    case property_id::topic_alias:
      if (position + 2 > property_data.size())
        return {std::move(decoded), length_bytes + property_length};
      property.value = read_u16(property_data.substr(position));
      position += 2;
      break;
    case property_id::message_expiry_interval:
    case property_id::session_expiry_interval:
    case property_id::will_delay_interval:
    case property_id::maximum_packet_size:
      if (position + 4 > property_data.size())
        return {std::move(decoded), length_bytes + property_length};
      property.value = read_u32(property_data.substr(position));
      position += 4;
      break;
    case property_id::subscription_identifier: {
      const auto [value, consumed] =
          decode_variable_length(property_data.substr(position));
      if (consumed == 0)
        return {std::move(decoded), length_bytes + property_length};
      property.value = static_cast<std::uint32_t>(value);
      position += consumed;
      break;
    }
    case property_id::content_type:
    case property_id::response_topic:
    case property_id::assigned_client_identifier:
    case property_id::authentication_method:
    case property_id::response_information:
    case property_id::server_reference:
    case property_id::reason_string:
    case property_id::correlation_data:
    case property_id::authentication_data: {
      auto value = read_string();
      if (!value)
        return {std::move(decoded), length_bytes + property_length};
      property.value = std::move(*value);
      break;
    }
    case property_id::user_property: {
      auto key = read_string();
      auto value = read_string();
      if (!key || !value)
        return {std::move(decoded), length_bytes + property_length};
      property.value = std::pair{std::move(*key), std::move(*value)};
      break;
    }
    default:
      return {std::move(decoded), length_bytes + property_length};
    }
    decoded.push_back(std::move(property));
  }
  return {std::move(decoded), length_bytes + property_length};
}
} // namespace cnetmod::mqtt::detail
