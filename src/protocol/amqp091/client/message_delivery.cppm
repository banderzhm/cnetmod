module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:message_delivery;
import std;
export import :message;

export namespace cnetmod::amqp091 {
struct delivery
{
    amqp091::message message;
    std::string consumer_tag;
    std::string exchange;
    std::string routing_key;
    std::uint64_t delivery_tag = 0;
    bool redelivered = false;
};

struct returned_message
{
    amqp091::message message;
    std::uint16_t reply_code = 0;
    std::string reply_text;
    std::string exchange;
    std::string routing_key;
};

using delivery_handler = std::function<void(const delivery&)>;
using return_handler = std::function<void(const returned_message&)>;
} // namespace cnetmod::amqp091
