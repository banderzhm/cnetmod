module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp091:message;

import std;

export namespace cnetmod::amqp091 {

struct message
{
    std::vector<std::byte> body;
    std::string content_type;
    std::string content_encoding;
    std::string message_id;
    std::string correlation_id;
    std::string reply_to;
    std::optional<std::chrono::milliseconds> ttl;
    std::optional<std::int64_t> timestamp;
    std::map<std::string, std::string, std::less<>> headers;
    bool durable = false;
};

} // namespace cnetmod::amqp091
