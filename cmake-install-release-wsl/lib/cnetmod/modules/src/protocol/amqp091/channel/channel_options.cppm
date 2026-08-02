module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:channel_options;
import std;

export namespace cnetmod::amqp091 {
enum class exchange_type
{
    direct,
    fanout,
    topic,
    headers,
    custom
};

struct exchange_declare_options
{
    std::string name;
    exchange_type type = exchange_type::direct;
    std::string custom_type;
    bool passive = false;
    bool durable = false;
    bool auto_delete = false;
    bool internal = false;
    bool no_wait = false;
};

struct queue_declare_options
{
    std::string name;
    bool passive = false;
    bool durable = false;
    bool exclusive = false;
    bool auto_delete = false;
    bool no_wait = false;
};

struct queue_declare_result
{
    std::string name;
    std::uint32_t message_count = 0;
    std::uint32_t consumer_count = 0;
};

struct binding_options
{
    std::string queue;
    std::string exchange;
    std::string routing_key;
    bool no_wait = false;
};

struct publish_options
{
    std::string exchange;
    std::string routing_key;
    bool mandatory = false;
    bool immediate = false;
};

struct consume_options
{
    std::string queue;
    std::string consumer_tag;
    bool no_local = false;
    bool no_ack = false;
    bool exclusive = false;
    bool no_wait = false;
};

struct qos_options
{
    std::uint32_t prefetch_size = 0;
    std::uint16_t prefetch_count = 0;
    bool global = false;
};

[[nodiscard]] auto exchange_type_name(const exchange_declare_options& options)
    -> std::string;
} // namespace cnetmod::amqp091
