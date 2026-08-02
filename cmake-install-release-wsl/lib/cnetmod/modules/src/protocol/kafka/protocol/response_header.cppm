module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.response_header;
import std;

export namespace cnetmod::kafka::protocol {
struct response_header
{
    std::int32_t correlation_id = 0;
};
} // namespace cnetmod::kafka::protocol
