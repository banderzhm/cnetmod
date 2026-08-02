module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp10:client_error;

import std;

export namespace cnetmod::amqp10 {

enum class error_stage
{
    configuration,
    resolution,
    transport,
    tls,
    authentication,
    handshake,
    protocol,
    flow_control,
    acknowledgement,
    transaction,
    cancelled,
};

struct error
{
    error_stage stage = error_stage::protocol;
    std::error_code code{};
    std::string message;
    bool retryable = false;
};

using disconnect_handler = std::function<void(const error&)>;

[[nodiscard]] auto to_string(error_stage value) noexcept -> std::string_view;

} // namespace cnetmod::amqp10
