/// cnetmod.protocol.mqtt:codec — MQTT Packet Encoding/Decoding
/// Supports all packet types for MQTT v3.1.1 and v5.0

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:codec;

import std;
import cnetmod.core.log;
import :types;

namespace cnetmod::mqtt {

using detail::encode_variable_length;
using detail::decode_variable_length;
using detail::write_u16;
using detail::read_u16;
using detail::write_u32;
using detail::read_u32;
using detail::write_utf8_string;
using detail::write_binary;

// =============================================================================
// V5 Properties Encoding/Decoding
// =============================================================================

namespace detail {

/// Determine value type for property ID and encode single property
export inline void encode_property(std::string& buf, const mqtt_property& prop) {
    // Property ID as variable length integer (actually 1 byte, since all IDs < 128)
    buf.push_back(static_cast<char>(static_cast<std::uint8_t>(prop.id)));

    std::visit([&](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::uint8_t>) {
            buf.push_back(static_cast<char>(val));
        } else if constexpr (std::is_same_v<T, std::uint16_t>) {
            write_u16(buf, val);
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            // subscription_identifier uses variable length encoding, others use 4 bytes
            if (prop.id == property_id::subscription_identifier) {
                encode_variable_length(buf, val);
            } else {
                write_u32(buf, val);
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            // correlation_data and authentication_data are binary
            if (prop.id == property_id::correlation_data ||
                prop.id == property_id::authentication_data) {
                write_binary(buf, val);
            } else {
                write_utf8_string(buf, val);
            }
        } else if constexpr (std::is_same_v<T, std::pair<std::string, std::string>>) {
            write_utf8_string(buf, val.first);
            write_utf8_string(buf, val.second);
        }
    }, prop.value);
}

/// Encode property list: first encode to temporary buffer, then write variable length + content
export inline void encode_properties(std::string& buf, const properties& props) {
    std::string prop_buf;
    for (auto& p : props)
        encode_property(prop_buf, p);
    encode_variable_length(buf, prop_buf.size());
    buf.append(prop_buf);
}

/// Decode property list, returns (property list, bytes consumed)
export inline auto decode_properties(std::string_view data)
    -> std::pair<properties, std::size_t>
{
    properties result;
    if (data.empty()) return {result, 0};

    auto [prop_len, vl_bytes] = decode_variable_length(data);
    if (vl_bytes == 0) return {result, 0};

    std::size_t total_consumed = vl_bytes;
    auto prop_data = data.substr(vl_bytes, prop_len);
    std::size_t pos = 0;

    while (pos < prop_data.size()) {
        auto id = static_cast<property_id>(static_cast<std::uint8_t>(prop_data[pos]));
        ++pos;

        mqtt_property prop;
        prop.id = id;

        switch (id) {
        // 1-byte properties
        case property_id::payload_format_indicator:
        case property_id::request_problem_information:
        case property_id::request_response_information:
        case property_id::maximum_qos:
        case property_id::retain_available:
        case property_id::wildcard_subscription_available:
        case property_id::subscription_identifier_available:
        case property_id::shared_subscription_available:
            if (pos >= prop_data.size()) goto done;
            prop.value = static_cast<std::uint8_t>(prop_data[pos]);
            ++pos;
            break;

        // 2-byte properties
        case property_id::server_keep_alive:
        case property_id::receive_maximum:
        case property_id::topic_alias_maximum:
        case property_id::topic_alias:
            if (pos + 2 > prop_data.size()) goto done;
            prop.value = read_u16(prop_data.substr(pos));
            pos += 2;
            break;

        // 4-byte properties
        case property_id::message_expiry_interval:
        case property_id::session_expiry_interval:
        case property_id::will_delay_interval:
        case property_id::maximum_packet_size:
            if (pos + 4 > prop_data.size()) goto done;
            prop.value = read_u32(prop_data.substr(pos));
            pos += 4;
            break;

        // Variable byte integer
        case property_id::subscription_identifier: {
            auto [val, consumed] = decode_variable_length(prop_data.substr(pos));
            if (consumed == 0) goto done;
            prop.value = static_cast<std::uint32_t>(val);
            pos += consumed;
            break;
        }

        // UTF-8 string properties
        case property_id::content_type:
        case property_id::response_topic:
        case property_id::assigned_client_identifier:
        case property_id::authentication_method:
        case property_id::response_information:
        case property_id::server_reference:
        case property_id::reason_string: {
            if (pos + 2 > prop_data.size()) goto done;
            auto slen = read_u16(prop_data.substr(pos));
            pos += 2;
            if (pos + slen > prop_data.size()) goto done;
            prop.value = std::string(prop_data.substr(pos, slen));
            pos += slen;
            break;
        }

        // Binary data properties
        case property_id::correlation_data:
        case property_id::authentication_data: {
            if (pos + 2 > prop_data.size()) goto done;
            auto slen = read_u16(prop_data.substr(pos));
            pos += 2;
            if (pos + slen > prop_data.size()) goto done;
            prop.value = std::string(prop_data.substr(pos, slen));
            pos += slen;
            break;
        }

        // String pair (user property)
        case property_id::user_property: {
            if (pos + 2 > prop_data.size()) goto done;
            auto klen = read_u16(prop_data.substr(pos));
            pos += 2;
            if (pos + klen > prop_data.size()) goto done;
            std::string key(prop_data.substr(pos, klen));
            pos += klen;
            if (pos + 2 > prop_data.size()) goto done;
            auto vlen = read_u16(prop_data.substr(pos));
            pos += 2;
            if (pos + vlen > prop_data.size()) goto done;
            std::string val(prop_data.substr(pos, vlen));
            pos += vlen;
            prop.value = std::pair{std::move(key), std::move(val)};
            break;
        }

        default:
            goto done; // Unknown property, stop parsing
        }

        result.push_back(std::move(prop));
    }

done:
    return {std::move(result), total_consumed + prop_len};
}

/// Build complete MQTT packet: fixed_header_byte + remaining_length + payload
/// When max_packet_size > 0, check if total length exceeds limit, return empty string if exceeded
export inline auto build_packet(std::uint8_t fixed_header, std::string_view payload,
                                 std::size_t max_packet_size = 0) -> std::string
{
    std::string pkt;
    pkt.reserve(1 + 4 + payload.size());
    pkt.push_back(static_cast<char>(fixed_header));
    encode_variable_length(pkt, payload.size());
    pkt.append(payload);
    // Check Maximum Packet Size (v5)
    if (max_packet_size > 0 && pkt.size() > max_packet_size) {
        return {}; // Exceeded limit
    }
    return pkt;
}

} // namespace detail

// =============================================================================
// CONNECT Encoding
// =============================================================================

export inline auto encode_connect(const connect_options& opts) -> std::string {
    std::string payload;
    payload.reserve(256);

    bool is_v5 = (opts.version == protocol_version::v5);

    // Variable header: Protocol Name + Protocol Level + Connect Flags + Keep Alive
    // Protocol Name: "MQTT"
    detail::write_utf8_string(payload, "MQTT");

    // Protocol Level
    payload.push_back(static_cast<char>(static_cast<std::uint8_t>(opts.version)));

    // Connect Flags
    std::uint8_t flags = 0;
    if (opts.clean_session) flags |= 0x02;
    if (opts.will_msg) {
        flags |= 0x04; // will flag
        flags |= (static_cast<std::uint8_t>(opts.will_msg->qos_value) << 3);
        if (opts.will_msg->retain) flags |= 0x20;
    }
    if (!opts.password.empty()) flags |= 0x40;
    if (!opts.username.empty()) flags |= 0x80;
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
        static_cast<std::uint8_t>(control_packet_type::connect) | 0x00,
        payload
    );
}

// =============================================================================
// CONNACK Decoding
// =============================================================================

/// v3.1.1 CONNACK result
export struct connack_result {
    bool                session_present = false;
    connect_return_code return_code     = connect_return_code::accepted;
    // v5 fields
    std::uint8_t        v5_reason       = 0;
    properties          props;
};

export inline auto decode_connack(std::string_view payload, protocol_version ver)
    -> std::expected<connack_result, std::string>
{
    if (payload.size() < 2) return std::unexpected("CONNACK too short");

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

export inline auto encode_publish(
    std::string_view topic,
    std::string_view payload_data,
    qos q,
    bool retain,
    bool dup,
    std::uint16_t packet_id,
    protocol_version ver,
    const properties& props = {}
) -> std::string
{
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
    if (dup)    fh |= 0x08;
    fh |= (static_cast<std::uint8_t>(q) << 1);
    if (retain) fh |= 0x01;

    return detail::build_packet(fh, payload);
}

/// Decode PUBLISH packet (from frame payload + flags)
export inline auto decode_publish(std::string_view payload, std::uint8_t flags, protocol_version ver)
    -> std::expected<publish_message, std::string>
{
    publish_message msg;
    msg.dup    = (flags & 0x08) != 0;
    msg.qos_value = static_cast<qos>((flags >> 1) & 0x03);
    msg.retain = (flags & 0x01) != 0;

    if (payload.size() < 2) return std::unexpected("PUBLISH too short");

    // Topic Name
    auto topic_len = detail::read_u16(payload);
    std::size_t pos = 2;
    if (pos + topic_len > payload.size()) return std::unexpected("PUBLISH topic overflow");
    msg.topic = std::string(payload.substr(pos, topic_len));
    pos += topic_len;

    // MQTT UTF-8 compliance validation
    if (!validate_utf8(msg.topic)) {
        logger::debug("mqtt decode_publish: invalid UTF-8 in topic");
        return std::unexpected("PUBLISH: invalid UTF-8 in topic");
    }

    // Packet Identifier
    if (msg.qos_value != qos::at_most_once) {
        if (pos + 2 > payload.size()) return std::unexpected("PUBLISH missing packet_id");
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

export inline auto encode_puback(std::uint16_t packet_id, protocol_version ver,
                                  std::uint8_t reason_code = 0,
                                  const properties& props = {}) -> std::string
{
    std::string payload;
    detail::write_u16(payload, packet_id);
    if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
        payload.push_back(static_cast<char>(reason_code));
        if (!props.empty()) detail::encode_properties(payload, props);
    }
    return detail::build_packet(static_cast<std::uint8_t>(control_packet_type::puback), payload);
}

export inline auto encode_pubrec(std::uint16_t packet_id, protocol_version ver,
                                  std::uint8_t reason_code = 0,
                                  const properties& props = {}) -> std::string
{
    std::string payload;
    detail::write_u16(payload, packet_id);
    if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
        payload.push_back(static_cast<char>(reason_code));
        if (!props.empty()) detail::encode_properties(payload, props);
    }
    return detail::build_packet(static_cast<std::uint8_t>(control_packet_type::pubrec), payload);
}

export inline auto encode_pubrel(std::uint16_t packet_id, protocol_version ver,
                                  std::uint8_t reason_code = 0,
                                  const properties& props = {}) -> std::string
{
    std::string payload;
    detail::write_u16(payload, packet_id);
    if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
        payload.push_back(static_cast<char>(reason_code));
        if (!props.empty()) detail::encode_properties(payload, props);
    }
    // PUBREL fixed header: 0x62 (type 6, flags 0010)
    return detail::build_packet(
        static_cast<std::uint8_t>(control_packet_type::pubrel) | 0x02,
        payload
    );
}

export inline auto encode_pubcomp(std::uint16_t packet_id, protocol_version ver,
                                   std::uint8_t reason_code = 0,
                                   const properties& props = {}) -> std::string
{
    std::string payload;
    detail::write_u16(payload, packet_id);
    if (ver == protocol_version::v5 && !(reason_code == 0 && props.empty())) {
        payload.push_back(static_cast<char>(reason_code));
        if (!props.empty()) detail::encode_properties(payload, props);
    }
    return detail::build_packet(static_cast<std::uint8_t>(control_packet_type::pubcomp), payload);
}

/// Decode ACK type packets (PUBACK/PUBREC/PUBREL/PUBCOMP)
export struct ack_result {
    std::uint16_t packet_id   = 0;
    std::uint8_t  reason_code = 0;  // v5
    properties    props;             // v5
};

export inline auto decode_ack(std::string_view payload, protocol_version ver)
    -> std::expected<ack_result, std::string>
{
    if (payload.size() < 2) return std::unexpected("ACK too short");
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

export inline auto encode_subscribe(
    std::uint16_t packet_id,
    const std::vector<subscribe_entry>& entries,
    protocol_version ver,
    const properties& props = {}
) -> std::string
{
    std::string payload;
    payload.reserve(128);

    // Packet Identifier
    detail::write_u16(payload, packet_id);

    // v5 Properties
    if (ver == protocol_version::v5) {
        detail::encode_properties(payload, props);
    }

    // Subscription entries
    for (auto& e : entries) {
        detail::write_utf8_string(payload, e.topic_filter);
        if (ver == protocol_version::v5) {
            payload.push_back(static_cast<char>(e.encode_options()));
        } else {
            payload.push_back(static_cast<char>(static_cast<std::uint8_t>(e.max_qos)));
        }
    }

    // SUBSCRIBE fixed header: 0x82 (type 8, flags 0010)
    return detail::build_packet(
        static_cast<std::uint8_t>(control_packet_type::subscribe) | 0x02,
        payload
    );
}

// =============================================================================
// SUBACK Decoding
// =============================================================================

export struct suback_result {
    std::uint16_t               packet_id = 0;
    std::vector<std::uint8_t>   return_codes;  // v3.1.1: suback_return_code, v5: suback_reason_code
    properties                  props;          // v5
};

export inline auto decode_suback(std::string_view payload, protocol_version ver)
    -> std::expected<suback_result, std::string>
{
    if (payload.size() < 2) return std::unexpected("SUBACK too short");
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

export inline auto encode_unsubscribe(
    std::uint16_t packet_id,
    const std::vector<std::string>& topic_filters,
    protocol_version ver,
    const properties& props = {}
) -> std::string
{
    std::string payload;
    payload.reserve(128);

    detail::write_u16(payload, packet_id);

    if (ver == protocol_version::v5) {
        detail::encode_properties(payload, props);
    }

    for (auto& tf : topic_filters) {
        detail::write_utf8_string(payload, tf);
    }

    // UNSUBSCRIBE fixed header: 0xA2 (type 10, flags 0010)
    return detail::build_packet(
        static_cast<std::uint8_t>(control_packet_type::unsubscribe) | 0x02,
        payload
    );
}

// =============================================================================
// UNSUBACK Decoding
// =============================================================================

export struct unsuback_result {
    std::uint16_t             packet_id = 0;
    std::vector<std::uint8_t> reason_codes; // v5 only
    properties                props;         // v5
};

export inline auto decode_unsuback(std::string_view payload, protocol_version ver)
    -> std::expected<unsuback_result, std::string>
{
    if (payload.size() < 2) return std::unexpected("UNSUBACK too short");
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

export inline auto encode_pingreq() -> std::string {
    return {static_cast<char>(static_cast<std::uint8_t>(control_packet_type::pingreq)),
            static_cast<char>(0)};
}

export inline auto encode_pingresp() -> std::string {
    return {static_cast<char>(static_cast<std::uint8_t>(control_packet_type::pingresp)),
            static_cast<char>(0)};
}

// =============================================================================
// DISCONNECT
// =============================================================================

export inline auto encode_disconnect(
    protocol_version ver,
    std::uint8_t reason_code = 0,
    const properties& props = {}
) -> std::string
{
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
    return {static_cast<char>(static_cast<std::uint8_t>(control_packet_type::disconnect)),
            static_cast<char>(0)};
}

/// Decode DISCONNECT
export struct disconnect_result {
    std::uint8_t reason_code = 0;  // v5
    properties   props;             // v5
};

export inline auto decode_disconnect(std::string_view payload, protocol_version ver)
    -> disconnect_result
{
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

export inline auto encode_auth(
    std::uint8_t reason_code = 0,
    const properties& props = {}
) -> std::string
{
    std::string payload;
    payload.push_back(static_cast<char>(reason_code));
    detail::encode_properties(payload, props);
    return detail::build_packet(
        static_cast<std::uint8_t>(control_packet_type::auth), payload);
}

export struct auth_result {
    std::uint8_t reason_code = 0;
    properties   props;
};

export inline auto decode_auth(std::string_view payload)
    -> auth_result
{
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
// ================== Broker-side Encoding/Decoding (Server-side codec) ==================
// =============================================================================

// =============================================================================
// CONNECT Decoding (Server receives)
// =============================================================================

export struct connect_data {
    protocol_version version       = protocol_version::v3_1_1;
    std::string      client_id;
    bool             clean_session = true;
    std::uint16_t    keep_alive    = 0;
    std::string      username;
    std::string      password;
    std::optional<will> will_msg;
    properties       props;        // v5 CONNECT properties
    properties       will_props;   // v5 Will properties
};

export inline auto decode_connect(std::string_view payload)
    -> std::expected<connect_data, std::string>
{
    connect_data r;
    if (payload.size() < 10) return std::unexpected("CONNECT too short");

    std::size_t pos = 0;

    // Protocol Name (length-prefixed "MQTT")
    if (pos + 2 > payload.size()) return std::unexpected("CONNECT: no protocol name length");
    auto name_len = detail::read_u16(payload.substr(pos)); pos += 2;
    if (pos + name_len > payload.size()) return std::unexpected("CONNECT: protocol name overflow");
    auto proto_name = payload.substr(pos, name_len); pos += name_len;
    if (proto_name != "MQTT") return std::unexpected("CONNECT: invalid protocol name");

    // Protocol Level
    if (pos >= payload.size()) return std::unexpected("CONNECT: no protocol level");
    auto level = static_cast<std::uint8_t>(payload[pos]); pos++;
    if (level == 4) r.version = protocol_version::v3_1_1;
    else if (level == 5) r.version = protocol_version::v5;
    else return std::unexpected("CONNECT: unsupported protocol level " + std::to_string(level));

    // Connect Flags
    if (pos >= payload.size()) return std::unexpected("CONNECT: no flags");
    auto flags = static_cast<std::uint8_t>(payload[pos]); pos++;
    // MQTT spec requires reserved bit (bit 0) must be 0
    if (flags & 0x01) return std::unexpected("CONNECT: reserved flag bit 0 must be 0");
    r.clean_session = (flags & 0x02) != 0;
    bool has_will      = (flags & 0x04) != 0;
    auto will_qos      = static_cast<qos>((flags >> 3) & 0x03);
    bool will_retain   = (flags & 0x20) != 0;
    bool has_password  = (flags & 0x40) != 0;
    bool has_username  = (flags & 0x80) != 0;

    // Keep Alive
    if (pos + 2 > payload.size()) return std::unexpected("CONNECT: no keep alive");
    r.keep_alive = detail::read_u16(payload.substr(pos)); pos += 2;

    // v5: CONNECT Properties
    bool is_v5 = (r.version == protocol_version::v5);
    if (is_v5) {
        auto [props, consumed] = detail::decode_properties(payload.substr(pos));
        r.props = std::move(props);
        pos += consumed;
    }

    // Payload: Client Identifier
    if (pos + 2 > payload.size()) return std::unexpected("CONNECT: no client_id length");
    auto cid_len = detail::read_u16(payload.substr(pos)); pos += 2;
    if (pos + cid_len > payload.size()) return std::unexpected("CONNECT: client_id overflow");
    r.client_id = std::string(payload.substr(pos, cid_len)); pos += cid_len;

    // MQTT UTF-8 compliance validation
    if (!r.client_id.empty() && !validate_utf8(r.client_id)) {
        logger::debug("mqtt decode_connect: invalid UTF-8 in client_id");
        return std::unexpected("CONNECT: invalid UTF-8 in client_id");
    }

    // Payload: Will
    if (has_will) {
        will w;
        w.qos_value = will_qos;
        w.retain    = will_retain;

        // v5: Will Properties
        if (is_v5) {
            auto [wp, wc] = detail::decode_properties(payload.substr(pos));
            r.will_props = std::move(wp);
            w.props = r.will_props;
            pos += wc;
        }

        // Will Topic
        if (pos + 2 > payload.size()) return std::unexpected("CONNECT: no will topic length");
        auto wt_len = detail::read_u16(payload.substr(pos)); pos += 2;
        if (pos + wt_len > payload.size()) return std::unexpected("CONNECT: will topic overflow");
        w.topic = std::string(payload.substr(pos, wt_len)); pos += wt_len;

        // Will Topic UTF-8 validation
        if (!validate_utf8(w.topic)) {
            logger::debug("mqtt decode_connect: invalid UTF-8 in will topic");
            return std::unexpected("CONNECT: invalid UTF-8 in will topic");
        }

        // Will Payload
        if (pos + 2 > payload.size()) return std::unexpected("CONNECT: no will payload length");
        auto wp_len = detail::read_u16(payload.substr(pos)); pos += 2;
        if (pos + wp_len > payload.size()) return std::unexpected("CONNECT: will payload overflow");
        w.message = std::string(payload.substr(pos, wp_len)); pos += wp_len;

        r.will_msg = std::move(w);
    }

    // Payload: Username
    if (has_username) {
        if (pos + 2 > payload.size()) return std::unexpected("CONNECT: no username length");
        auto u_len = detail::read_u16(payload.substr(pos)); pos += 2;
        if (pos + u_len > payload.size()) return std::unexpected("CONNECT: username overflow");
        r.username = std::string(payload.substr(pos, u_len)); pos += u_len;

        if (!validate_utf8(r.username)) {
            logger::debug("mqtt decode_connect: invalid UTF-8 in username");
            return std::unexpected("CONNECT: invalid UTF-8 in username");
        }
    }

    // Payload: Password
    if (has_password) {
        if (pos + 2 > payload.size()) return std::unexpected("CONNECT: no password length");
        auto p_len = detail::read_u16(payload.substr(pos)); pos += 2;
        if (pos + p_len > payload.size()) return std::unexpected("CONNECT: password overflow");
        r.password = std::string(payload.substr(pos, p_len)); pos += p_len;
    }

    return r;
}

// =============================================================================
// CONNACK Encoding (Server sends)
// =============================================================================

export inline auto encode_connack(
    bool session_present,
    std::uint8_t return_code,
    protocol_version ver,
    const properties& props = {}
) -> std::string
{
    std::string payload;
    payload.reserve(32);

    // Connect Acknowledge Flags
    payload.push_back(session_present ? static_cast<char>(0x01) : static_cast<char>(0x00));

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

export struct subscribe_data {
    std::uint16_t                  packet_id = 0;
    std::vector<subscribe_entry>   entries;
    properties                     props;    // v5
};

export inline auto decode_subscribe(std::string_view payload, protocol_version ver)
    -> std::expected<subscribe_data, std::string>
{
    if (payload.size() < 5) return std::unexpected("SUBSCRIBE too short");
    subscribe_data r;
    std::size_t pos = 0;

    // Packet Identifier
    r.packet_id = detail::read_u16(payload.substr(pos)); pos += 2;

    // v5 Properties
    if (ver == protocol_version::v5) {
        auto [props, consumed] = detail::decode_properties(payload.substr(pos));
        r.props = std::move(props);
        pos += consumed;
    }

    // v5: Extract Subscription Identifier
    std::uint32_t sub_id = 0;
    if (ver == protocol_version::v5) {
        for (auto& p : r.props)
            if (p.id == property_id::subscription_identifier)
                if (auto* v = std::get_if<std::uint32_t>(&p.value))
                    sub_id = *v;
    }

    // Subscription entries
    while (pos < payload.size()) {
        if (pos + 2 > payload.size()) break;
        auto tf_len = detail::read_u16(payload.substr(pos)); pos += 2;
        if (pos + tf_len > payload.size()) break;
        std::string topic_filter(payload.substr(pos, tf_len)); pos += tf_len;

        if (pos >= payload.size()) break;
        auto opts_byte = static_cast<std::uint8_t>(payload[pos]); pos++;

        // MQTT UTF-8 compliance validation
        if (!validate_utf8(topic_filter)) {
            logger::debug("mqtt decode_subscribe: invalid UTF-8 in topic_filter");
            return std::unexpected("SUBSCRIBE: invalid UTF-8 in topic_filter");
        }

        subscribe_entry entry;
        entry.topic_filter = std::move(topic_filter);
        entry.max_qos = static_cast<qos>(opts_byte & 0x03);
        if (ver == protocol_version::v5) {
            entry.no_local            = (opts_byte & 0x04) != 0;
            entry.retain_as_published = (opts_byte & 0x08) != 0;
            entry.rh = static_cast<retain_handling>((opts_byte >> 4) & 0x03);
            entry.subscription_id     = sub_id;
        }
        r.entries.push_back(std::move(entry));
    }

    return r;
}

// =============================================================================
// SUBACK Encoding (Server sends)
// =============================================================================

export inline auto encode_suback(
    std::uint16_t packet_id,
    const std::vector<std::uint8_t>& return_codes,
    protocol_version ver,
    const properties& props = {}
) -> std::string
{
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

export struct unsubscribe_data {
    std::uint16_t              packet_id = 0;
    std::vector<std::string>   topic_filters;
    properties                 props;    // v5
};

export inline auto decode_unsubscribe(std::string_view payload, protocol_version ver)
    -> std::expected<unsubscribe_data, std::string>
{
    if (payload.size() < 4) return std::unexpected("UNSUBSCRIBE too short");
    unsubscribe_data r;
    std::size_t pos = 0;

    // Packet Identifier
    r.packet_id = detail::read_u16(payload.substr(pos)); pos += 2;

    // v5 Properties
    if (ver == protocol_version::v5) {
        auto [props, consumed] = detail::decode_properties(payload.substr(pos));
        r.props = std::move(props);
        pos += consumed;
    }

    // Topic Filters
    while (pos < payload.size()) {
        if (pos + 2 > payload.size()) break;
        auto tf_len = detail::read_u16(payload.substr(pos)); pos += 2;
        if (pos + tf_len > payload.size()) break;
        r.topic_filters.emplace_back(payload.substr(pos, tf_len)); pos += tf_len;
    }

    return r;
}

// =============================================================================
// UNSUBACK Encoding (Server sends)
// =============================================================================

export inline auto encode_unsuback(
    std::uint16_t packet_id,
    protocol_version ver,
    const std::vector<std::uint8_t>& reason_codes = {},
    const properties& props = {}
) -> std::string
{
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
