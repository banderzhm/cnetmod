module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:message_section;
import std;
import :primitive_value;

export namespace cnetmod::amqp10 {
struct header_section
{
    bool durable = false;
    std::uint8_t priority = 4;
    std::optional<std::chrono::milliseconds> ttl;
    bool first_acquirer = false;
    std::uint32_t delivery_count = 0;
};

struct properties_section
{
    std::optional<value> message_id;
    binary user_id;
    std::string to;
    std::string subject;
    std::string reply_to;
    std::optional<value> correlation_id;
    std::string content_type;
    std::string content_encoding;
    std::optional<timestamp> absolute_expiry_time;
    std::optional<timestamp> creation_time;
    std::string group_id;
    std::optional<std::uint32_t> group_sequence;
    std::string reply_to_group_id;
};

using annotations = std::map<symbol, value, std::less<>>;
using application_properties = std::map<std::string, value, std::less<>>;
using message_body = std::variant<binary, value, std::vector<list>>;

struct message
{
    std::optional<header_section> header;
    annotations delivery_annotations;
    annotations message_annotations;
    std::optional<properties_section> properties;
    application_properties application;
    message_body body = binary{};
    annotations footer;
};

[[nodiscard]] auto encode_message(const message&) -> binary;
[[nodiscard]] auto decode_message(std::span<const std::byte>)
    -> std::expected<message, std::error_code>;
} // namespace cnetmod::amqp10
