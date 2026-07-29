module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.amqp10;

import :client_error;
import std;

namespace cnetmod::amqp10 {

auto to_string(error_stage value) noexcept -> std::string_view
{
    switch (value)
    {
    case error_stage::configuration:
        return "configuration";
    case error_stage::resolution:
        return "resolution";
    case error_stage::transport:
        return "transport";
    case error_stage::tls:
        return "tls";
    case error_stage::authentication:
        return "authentication";
    case error_stage::handshake:
        return "handshake";
    case error_stage::protocol:
        return "protocol";
    case error_stage::flow_control:
        return "flow-control";
    case error_stage::acknowledgement:
        return "acknowledgement";
    case error_stage::transaction:
        return "transaction";
    case error_stage::cancelled:
        return "cancelled";
    }
    return "unknown";
}

} // namespace cnetmod::amqp10
