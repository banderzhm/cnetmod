export module cnetmod.protocol.mongodb:error;

import std;

export namespace cnetmod::mongodb {

enum class error_code
{
    invalid_bson,
    message_too_large,
    protocol_error,
    connection_failed,
    tls_failed,
    authentication_failed,
    compression_failed,
    server_selection_failed,
    pool_exhausted,
    transaction_failed,
    change_stream_closed,
    operation_timed_out,
    operation_cancelled,
    command_failed,
    connection_closed
};

struct error
{
    error_code code = error_code::protocol_error;
    std::string message;
    std::int32_t server_code = 0;
    std::string server_code_name;
    std::map<std::string, std::string> labels;
};

template <class T>
using result = std::expected<T, error>;

inline auto make_error(error_code code, std::string message) -> error
{
    return error{.code = code, .message = std::move(message)};
}

} // namespace cnetmod::mongodb
