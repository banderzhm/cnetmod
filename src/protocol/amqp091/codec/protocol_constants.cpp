module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :protocol_constants;
import std;

namespace cnetmod::amqp091 {
auto make_error(error_code code, std::string message, bool retryable) -> error
{
    return error{
        .code = code,
        .message = std::move(message),
        .retryable = retryable};
}
} // namespace cnetmod::amqp091
