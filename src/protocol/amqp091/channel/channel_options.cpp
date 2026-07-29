module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :channel_options;
import std;

namespace cnetmod::amqp091 {
auto exchange_type_name(const exchange_declare_options& o) -> std::string
{
    switch (o.type)
    {
    case exchange_type::direct:
        return "direct";
    case exchange_type::fanout:
        return "fanout";
    case exchange_type::topic:
        return "topic";
    case exchange_type::headers:
        return "headers";
    case exchange_type::custom:
        return o.custom_type;
    }
    return {};
}
} // namespace cnetmod::amqp091
