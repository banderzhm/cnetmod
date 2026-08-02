module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:protocol_error;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;

export namespace cnetmod::amqp10 {
enum class errc
{
    invalid_field = 1,
    malformed_frame,
    unexpected_performative,
    frame_size_too_small,
    frame_size_too_large,
    idle_timeout,
    link_credit_exhausted,
    delivery_rejected,
    authentication_failed,
    protocol_state,
    connection_closed,
    cancelled,
    transaction_failed
};
[[nodiscard]] auto error_category() noexcept -> const std::error_category&;
[[nodiscard]] auto make_error_code(errc) noexcept -> std::error_code;
[[nodiscard]] auto make_error(error_stage, errc, std::string,
    bool retryable = false) -> error;
} // namespace cnetmod::amqp10

template <>
struct std::is_error_code_enum<cnetmod::amqp10::errc> : true_type
{
};
