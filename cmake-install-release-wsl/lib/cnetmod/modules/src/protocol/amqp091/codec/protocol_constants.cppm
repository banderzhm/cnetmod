module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:protocol_constants;
import std;

export namespace cnetmod::amqp091 {
inline constexpr std::array<std::byte, 8> protocol_header{
    std::byte{'A'}, std::byte{'M'}, std::byte{'Q'}, std::byte{'P'},
    std::byte{0}, std::byte{0}, std::byte{9}, std::byte{1}};
enum class error_code
{
    malformed_frame,
    frame_too_large,
    unexpected_frame,
    invalid_field,
    invalid_channel,
    connection_closed,
    channel_closed,
    access_refused,
    not_found,
    resource_locked,
    precondition_failed,
    command_invalid,
    timeout,
    cancelled
};

struct error
{
    error_code code = error_code::command_invalid;
    std::string message;
    std::uint16_t reply_code = 0;
    std::uint16_t class_id = 0;
    std::uint16_t method_id = 0;
    bool retryable = false;
};

template <typename T> using result = std::expected<T, error>;
enum class frame_type : std::uint8_t
{
    method = 1,
    header = 2,
    body = 3,
    heartbeat = 8
};
inline constexpr std::byte frame_end{0xCE};
enum class connection_state
{
    disconnected,
    connecting,
    authenticating,
    opening,
    open,
    recovering,
    closing
};
[[nodiscard]] auto make_error(error_code code, std::string message,
    bool retryable = false) -> error;
} // namespace cnetmod::amqp091
