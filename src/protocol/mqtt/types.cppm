/// cnetmod.protocol.mqtt:types — MQTT basic type definitions
/// Supports MQTT v3.1.1 and v5.0

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:types;

import std;

namespace cnetmod::mqtt {

// =============================================================================
// Protocol version
// =============================================================================

export enum class protocol_version : std::uint8_t {
    v3_1_1 = 4,
    v5     = 5,
};

export constexpr auto to_string(protocol_version v) noexcept -> std::string_view {
    switch (v) {
        case protocol_version::v3_1_1: return "v3.1.1";
        case protocol_version::v5:     return "v5.0";
        default:                       return "unknown";
    }
}

// =============================================================================
// QoS
// =============================================================================

export enum class qos : std::uint8_t {
    at_most_once  = 0,
    at_least_once = 1,
    exactly_once  = 2,
};

export constexpr auto to_string(qos q) noexcept -> std::string_view {
    switch (q) {
        case qos::at_most_once:  return "at_most_once";
        case qos::at_least_once: return "at_least_once";
        case qos::exactly_once:  return "exactly_once";
        default:                 return "invalid_qos";
    }
}

// =============================================================================
// Control packet types
// =============================================================================

export enum class control_packet_type : std::uint8_t {
    connect     = 0x10,
    connack     = 0x20,
    publish     = 0x30,
    puback      = 0x40,
    pubrec      = 0x50,
    pubrel      = 0x60,
    pubcomp     = 0x70,
    subscribe   = 0x80,
    suback      = 0x90,
    unsubscribe = 0xA0,
    unsuback    = 0xB0,
    pingreq     = 0xC0,
    pingresp    = 0xD0,
    disconnect  = 0xE0,
    auth        = 0xF0,
};

export constexpr auto get_packet_type(std::uint8_t byte) noexcept -> control_packet_type {
    return static_cast<control_packet_type>(byte & 0xF0);
}

export constexpr auto get_packet_flags(std::uint8_t byte) noexcept -> std::uint8_t {
    return byte & 0x0F;
}

export constexpr auto to_string(control_packet_type t) noexcept -> std::string_view {
    switch (t) {
        case control_packet_type::connect:     return "CONNECT";
        case control_packet_type::connack:     return "CONNACK";
        case control_packet_type::publish:     return "PUBLISH";
        case control_packet_type::puback:      return "PUBACK";
        case control_packet_type::pubrec:      return "PUBREC";
        case control_packet_type::pubrel:      return "PUBREL";
        case control_packet_type::pubcomp:     return "PUBCOMP";
        case control_packet_type::subscribe:   return "SUBSCRIBE";
        case control_packet_type::suback:      return "SUBACK";
        case control_packet_type::unsubscribe: return "UNSUBSCRIBE";
        case control_packet_type::unsuback:    return "UNSUBACK";
        case control_packet_type::pingreq:     return "PINGREQ";
        case control_packet_type::pingresp:    return "PINGRESP";
        case control_packet_type::disconnect:  return "DISCONNECT";
        case control_packet_type::auth:        return "AUTH";
        default:                               return "UNKNOWN";
    }
}

// =============================================================================
// CONNECT return codes (v3.1.1)
// =============================================================================

export enum class connect_return_code : std::uint8_t {
    accepted                      = 0,
    unacceptable_protocol_version = 1,
    identifier_rejected           = 2,
    server_unavailable            = 3,
    bad_user_name_or_password     = 4,
    not_authorized                = 5,
};

export constexpr auto to_string(connect_return_code c) noexcept -> std::string_view {
    switch (c) {
        case connect_return_code::accepted:                      return "accepted";
        case connect_return_code::unacceptable_protocol_version: return "unacceptable_protocol_version";
        case connect_return_code::identifier_rejected:           return "identifier_rejected";
        case connect_return_code::server_unavailable:            return "server_unavailable";
        case connect_return_code::bad_user_name_or_password:     return "bad_user_name_or_password";
        case connect_return_code::not_authorized:                return "not_authorized";
        default:                                                 return "unknown";
    }
}

// =============================================================================
// SUBACK return codes (v3.1.1)
// =============================================================================

export enum class suback_return_code : std::uint8_t {
    success_max_qos_0 = 0x00,
    success_max_qos_1 = 0x01,
    success_max_qos_2 = 0x02,
    failure           = 0x80,
};

// =============================================================================
// MQTT v5 Reason Codes
// =============================================================================

namespace v5 {

export enum class connect_reason_code : std::uint8_t {
    success                       = 0x00,
    unspecified_error             = 0x80,
    malformed_packet              = 0x81,
    protocol_error                = 0x82,
    implementation_specific_error = 0x83,
    unsupported_protocol_version  = 0x84,
    client_identifier_not_valid   = 0x85,
    bad_user_name_or_password     = 0x86,
    not_authorized                = 0x87,
    server_unavailable            = 0x88,
    server_busy                   = 0x89,
    banned                        = 0x8A,
    server_shutting_down          = 0x8B,
    bad_authentication_method     = 0x8C,
    topic_name_invalid            = 0x90,
    packet_too_large              = 0x95,
    quota_exceeded                = 0x97,
    payload_format_invalid        = 0x99,
    retain_not_supported          = 0x9A,
    qos_not_supported             = 0x9B,
    use_another_server            = 0x9C,
    server_moved                  = 0x9D,
    connection_rate_exceeded      = 0x9F,
};

export constexpr auto is_error(connect_reason_code c) noexcept -> bool {
    return static_cast<std::uint8_t>(c) >= 0x80;
}

export enum class disconnect_reason_code : std::uint8_t {
    normal_disconnection                   = 0x00,
    disconnect_with_will_message           = 0x04,
    unspecified_error                      = 0x80,
    malformed_packet                       = 0x81,
    protocol_error                         = 0x82,
    implementation_specific_error          = 0x83,
    not_authorized                         = 0x87,
    server_busy                            = 0x89,
    server_shutting_down                   = 0x8B,
    keep_alive_timeout                     = 0x8D,
    session_taken_over                     = 0x8E,
    topic_filter_invalid                   = 0x8F,
    topic_name_invalid                     = 0x90,
    receive_maximum_exceeded               = 0x93,
    topic_alias_invalid                    = 0x94,
    packet_too_large                       = 0x95,
    message_rate_too_high                  = 0x96,
    quota_exceeded                         = 0x97,
    administrative_action                  = 0x98,
    payload_format_invalid                 = 0x99,
    retain_not_supported                   = 0x9A,
    qos_not_supported                      = 0x9B,
    use_another_server                     = 0x9C,
    server_moved                           = 0x9D,
    shared_subscriptions_not_supported     = 0x9E,
    connection_rate_exceeded               = 0x9F,
    maximum_connect_time                   = 0xA0,
    subscription_identifiers_not_supported = 0xA1,
    wildcard_subscriptions_not_supported   = 0xA2,
};

export enum class suback_reason_code : std::uint8_t {
    granted_qos_0                          = 0x00,
    granted_qos_1                          = 0x01,
    granted_qos_2                          = 0x02,
    unspecified_error                      = 0x80,
    implementation_specific_error          = 0x83,
    not_authorized                         = 0x87,
    topic_filter_invalid                   = 0x8F,
    packet_identifier_in_use               = 0x91,
    quota_exceeded                         = 0x97,
    shared_subscriptions_not_supported     = 0x9E,
    subscription_identifiers_not_supported = 0xA1,
    wildcard_subscriptions_not_supported   = 0xA2,
};

export constexpr auto is_error(suback_reason_code c) noexcept -> bool {
    return static_cast<std::uint8_t>(c) >= 0x80;
}

export enum class unsuback_reason_code : std::uint8_t {
    success                       = 0x00,
    no_subscription_existed       = 0x11,
    unspecified_error             = 0x80,
    implementation_specific_error = 0x83,
    not_authorized                = 0x87,
    topic_filter_invalid          = 0x8F,
    packet_identifier_in_use      = 0x91,
};

export enum class puback_reason_code : std::uint8_t {
    success                       = 0x00,
    no_matching_subscribers       = 0x10,
    unspecified_error             = 0x80,
    implementation_specific_error = 0x83,
    not_authorized                = 0x87,
    topic_name_invalid            = 0x90,
    packet_identifier_in_use      = 0x91,
    quota_exceeded                = 0x97,
    payload_format_invalid        = 0x99,
};

export constexpr auto is_error(puback_reason_code c) noexcept -> bool {
    return static_cast<std::uint8_t>(c) >= 0x80;
}

export enum class pubrec_reason_code : std::uint8_t {
    success                       = 0x00,
    no_matching_subscribers       = 0x10,
    unspecified_error             = 0x80,
    implementation_specific_error = 0x83,
    not_authorized                = 0x87,
    topic_name_invalid            = 0x90,
    packet_identifier_in_use      = 0x91,
    quota_exceeded                = 0x97,
    payload_format_invalid        = 0x99,
};

export constexpr auto is_error(pubrec_reason_code c) noexcept -> bool {
    return static_cast<std::uint8_t>(c) >= 0x80;
}

export enum class pubrel_reason_code : std::uint8_t {
    success                     = 0x00,
    packet_identifier_not_found = 0x92,
};

export enum class pubcomp_reason_code : std::uint8_t {
    success                     = 0x00,
    packet_identifier_not_found = 0x92,
};

export enum class auth_reason_code : std::uint8_t {
    success                 = 0x00,
    continue_authentication = 0x18,
    re_authenticate         = 0x19,
};

} // namespace v5

// =============================================================================
// MQTT v5 Property System
// =============================================================================

export enum class property_id : std::uint8_t {
    payload_format_indicator          =  1,
    message_expiry_interval           =  2,
    content_type                      =  3,
    response_topic                    =  8,
    correlation_data                  =  9,
    subscription_identifier           = 11,
    session_expiry_interval           = 17,
    assigned_client_identifier        = 18,
    server_keep_alive                 = 19,
    authentication_method             = 21,
    authentication_data               = 22,
    request_problem_information       = 23,
    will_delay_interval               = 24,
    request_response_information      = 25,
    response_information              = 26,
    server_reference                  = 28,
    reason_string                     = 31,
    receive_maximum                   = 33,
    topic_alias_maximum               = 34,
    topic_alias                       = 35,
    maximum_qos                       = 36,
    retain_available                  = 37,
    user_property                     = 38,
    maximum_packet_size               = 39,
    wildcard_subscription_available   = 40,
    subscription_identifier_available = 41,
    shared_subscription_available     = 42,
};

/// Property value type
export struct mqtt_property {
    property_id id;
    std::variant<
        std::uint8_t,                                  // byte
        std::uint16_t,                                 // two byte integer
        std::uint32_t,                                 // four byte integer / variable byte integer
        std::string,                                   // UTF-8 string / binary data
        std::pair<std::string, std::string>            // string pair (user property)
    > value;

    /// Convenience constructors
    static auto byte_prop(property_id id, std::uint8_t v) -> mqtt_property {
        return {id, v};
    }
    static auto u16_prop(property_id id, std::uint16_t v) -> mqtt_property {
        return {id, v};
    }
    static auto u32_prop(property_id id, std::uint32_t v) -> mqtt_property {
        return {id, v};
    }
    static auto string_prop(property_id id, std::string v) -> mqtt_property {
        return {id, std::move(v)};
    }
    static auto binary_prop(property_id id, std::string v) -> mqtt_property {
        return {id, std::move(v)};
    }
    static auto string_pair_prop(property_id id, std::string key, std::string val) -> mqtt_property {
        return {id, std::pair{std::move(key), std::move(val)}};
    }
};

export using properties = std::vector<mqtt_property>;

/// v5 AUTH callback (Enhanced Authentication)
/// Parameters: client_id, reason_code, props
/// Returns: response (reason_code, props), or nullopt for no response
export using broker_auth_handler = std::function<
    std::optional<std::pair<std::uint8_t, properties>>(
        const std::string& client_id, std::uint8_t reason_code,
        const properties& props)>;

// =============================================================================
// Core structures
// =============================================================================

/// Will message
export struct will {
    std::string topic;
    std::string message;
    qos         qos_value = qos::at_most_once;
    bool        retain    = false;
    properties  props;     // v5 only
};

/// v5 subscription options
export enum class retain_handling : std::uint8_t {
    send                      = 0,
    send_only_new_subscription = 1,
    not_send                  = 2,
};

/// Subscription entry
export struct subscribe_entry {
    std::string      topic_filter;
    qos              max_qos         = qos::at_most_once;
    // v5 subscribe options
    bool             no_local            = false;
    bool             retain_as_published = false;
    retain_handling  rh                  = retain_handling::send;
    std::uint32_t    subscription_id     = 0;   // v5 Subscription Identifier

    /// Encode as v5 subscribe options byte
    [[nodiscard]] auto encode_options() const noexcept -> std::uint8_t {
        std::uint8_t opts = static_cast<std::uint8_t>(max_qos);
        if (no_local)            opts |= 0x04;
        if (retain_as_published) opts |= 0x08;
        opts |= (static_cast<std::uint8_t>(rh) << 4);
        return opts;
    }
};

/// Received PUBLISH message
export struct publish_message {
    std::string      topic;
    std::string      payload;
    qos              qos_value  = qos::at_most_once;
    bool             retain     = false;
    bool             dup        = false;
    std::uint16_t    packet_id  = 0;
    properties       props;     // v5 only

    /// Offline queue enqueue timestamp (for message_expiry_interval check)
    std::chrono::steady_clock::time_point enqueue_time{};
};

/// Connection options
export struct connect_options {
    std::string      host           = "127.0.0.1";
    std::uint16_t    port           = 1883;
    std::string      client_id;
    bool             clean_session  = true;  // v3.1.1: clean_session, v5: clean_start
    std::uint16_t    keep_alive_sec = 60;
    std::string      username;
    std::string      password;
    std::optional<will> will_msg;
    protocol_version version       = protocol_version::v3_1_1;
    properties       props;        // v5 CONNECT properties

    // Timeout
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(30);

    // TLS
    bool        tls           = false;
    bool        tls_verify    = true;
    std::string tls_ca_file;
    std::string tls_cert_file;
    std::string tls_key_file;
    std::string tls_sni;
};

// =============================================================================
// Error codes
// =============================================================================

export enum class mqtt_errc {
    success = 0,

    // Protocol errors
    malformed_packet,
    protocol_error,
    invalid_remaining_length,
    invalid_packet_type,
    invalid_qos,
    packet_too_large,

    // Connection errors
    not_connected,
    connect_refused,
    connect_timeout,
    keep_alive_timeout,

    // General
    unexpected_disconnect,
    unknown_error,
};

namespace detail {

class mqtt_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override { return "mqtt"; }
    auto message(int ev) const -> std::string override {
        switch (static_cast<mqtt_errc>(ev)) {
            case mqtt_errc::success:                  return "success";
            case mqtt_errc::malformed_packet:         return "malformed packet";
            case mqtt_errc::protocol_error:           return "protocol error";
            case mqtt_errc::invalid_remaining_length: return "invalid remaining length";
            case mqtt_errc::invalid_packet_type:      return "invalid packet type";
            case mqtt_errc::invalid_qos:              return "invalid qos";
            case mqtt_errc::packet_too_large:         return "packet too large";
            case mqtt_errc::not_connected:            return "not connected";
            case mqtt_errc::connect_refused:          return "connection refused";
            case mqtt_errc::connect_timeout:          return "connect timeout";
            case mqtt_errc::keep_alive_timeout:       return "keep alive timeout";
            case mqtt_errc::unexpected_disconnect:    return "unexpected disconnect";
            case mqtt_errc::unknown_error:            return "unknown error";
            default:                                  return "unrecognized mqtt error";
        }
    }
};

inline auto mqtt_category_instance() -> const std::error_category& {
    static const mqtt_error_category_impl instance;
    return instance;
}

} // namespace detail

export inline auto make_error_code(mqtt_errc e) noexcept -> std::error_code {
    return {static_cast<int>(e), detail::mqtt_category_instance()};
}

// =============================================================================
// UTF-8 string validation (MQTT spec requirement)
// =============================================================================

/// Validate UTF-8 compliance as required by MQTT spec
/// Forbidden: U+0000, U+0001-001F control characters (except U+000D, U+000A),
///            U+007F-009F control characters, U+D800-DFFF surrogates, U+FFFE/FFFF
/// Also validates UTF-8 encoding itself (no truncated sequences, no overlong encoding)
export inline auto validate_utf8(std::string_view s) noexcept -> bool {
    std::size_t i = 0;
    while (i < s.size()) {
        auto b0 = static_cast<std::uint8_t>(s[i]);

        std::uint32_t cp = 0;
        std::size_t   len = 0;

        if (b0 <= 0x7F) {
            cp = b0; len = 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 >= s.size()) return false;
            auto b1 = static_cast<std::uint8_t>(s[i + 1]);
            if ((b1 & 0xC0) != 0x80) return false;
            cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
            if (cp < 0x80) return false; // overlong
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 >= s.size()) return false;
            auto b1 = static_cast<std::uint8_t>(s[i + 1]);
            auto b2 = static_cast<std::uint8_t>(s[i + 2]);
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
            cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            if (cp < 0x800) return false; // overlong
            len = 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 >= s.size()) return false;
            auto b1 = static_cast<std::uint8_t>(s[i + 1]);
            auto b2 = static_cast<std::uint8_t>(s[i + 2]);
            auto b3 = static_cast<std::uint8_t>(s[i + 3]);
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
                return false;
            cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) |
                 ((b2 & 0x3F) << 6)  | (b3 & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) return false; // overlong or out of range
            len = 4;
        } else {
            return false; // Invalid start byte
        }

        // MQTT forbidden codepoints
        if (cp == 0x0000) return false;                        // U+0000 (null)
        if (cp >= 0x0001 && cp <= 0x001F && cp != 0x000A && cp != 0x000D)
            return false;                                      // Control characters (preserve LF, CR)
        if (cp >= 0x007F && cp <= 0x009F) return false;        // DEL + C1 control characters
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;        // Surrogates
        if (cp == 0xFFFE || cp == 0xFFFF) return false;        // Non-characters

        i += len;
    }
    return true;
}

// =============================================================================
// Variable Length Encoding (MQTT Remaining Length)
// =============================================================================

namespace detail {

/// Encode variable length integer to end of string
export inline void encode_variable_length(std::string& buf, std::size_t value) {
    if (value > 0x0FFFFFFF) return; // Maximum 268,435,455
    do {
        auto byte = static_cast<char>(value & 0x7F);
        value >>= 7;
        if (value > 0) byte |= static_cast<char>(0x80);
        buf.push_back(byte);
    } while (value > 0);
}

/// Decode variable length integer, returns (value, consumed_bytes); returns (0, 0) on failure
export constexpr auto decode_variable_length(std::string_view data) noexcept
    -> std::pair<std::size_t, std::size_t>
{
    std::size_t value = 0;
    std::size_t multiplier = 1;
    std::size_t consumed = 0;
    for (std::size_t i = 0; i < data.size() && i < 4; ++i) {
        auto byte = static_cast<std::uint8_t>(data[i]);
        value += (byte & 0x7F) * multiplier;
        multiplier *= 128;
        ++consumed;
        if (!(byte & 0x80)) return {value, consumed};
    }
    return {0, 0}; // Incomplete or invalid
}

/// Write 16-bit big-endian integer
export inline void write_u16(std::string& buf, std::uint16_t v) {
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>(v & 0xFF));
}

/// Read 16-bit big-endian integer
export constexpr auto read_u16(std::string_view data) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint8_t>(data[0]) << 8) |
         static_cast<std::uint8_t>(data[1]));
}

/// Write 32-bit big-endian integer
export inline void write_u32(std::string& buf, std::uint32_t v) {
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>(v & 0xFF));
}

/// Read 32-bit big-endian integer
export constexpr auto read_u32(std::string_view data) noexcept -> std::uint32_t {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[2])) <<  8) |
            static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[3]));
}

/// Write UTF-8 prefixed string (2-byte length + content)
export inline void write_utf8_string(std::string& buf, std::string_view s) {
    write_u16(buf, static_cast<std::uint16_t>(s.size()));
    buf.append(s);
}

/// Write binary data (2-byte length + content)
export inline void write_binary(std::string& buf, std::string_view s) {
    write_u16(buf, static_cast<std::uint16_t>(s.size()));
    buf.append(s);
}

} // namespace detail

} // namespace cnetmod::mqtt

template <>
struct std::is_error_code_enum<cnetmod::mqtt::mqtt_errc> : std::true_type {};
