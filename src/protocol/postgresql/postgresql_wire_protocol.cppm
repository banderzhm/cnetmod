export module cnetmod.protocol.postgresql:wire_protocol;

import std;
import :connection_options;
import :query_result;

namespace cnetmod::postgresql::detail {

struct backend_message
{
    char type{};
    std::vector<std::uint8_t> payload;
};

struct server_error
{
    std::string severity;
    std::string sql_state;
    std::string message;
    std::string detail;
    std::string hint;
};

auto startup_message(const connection_options&) -> std::vector<std::uint8_t>;
auto ssl_request() -> std::array<std::uint8_t, 8>;
auto password_message(std::string_view) -> std::vector<std::uint8_t>;
auto simple_query_message(std::string_view) -> std::vector<std::uint8_t>;
auto terminate_message() -> std::array<std::uint8_t, 5>;
auto parse_message(std::span<const std::uint8_t>)
    -> std::expected<backend_message, std::string>;
auto parse_error(std::span<const std::uint8_t>) -> server_error;
auto md5_password(std::string_view user, std::string_view password,
    std::span<const std::uint8_t, 4> salt)
    -> std::expected<std::string, std::string>;

struct scram_client
{
    std::string nonce;
    std::string client_first_bare;
    std::string server_first;
    std::string auth_message;
    std::vector<std::uint8_t> server_signature;
    auto begin(std::string_view username) -> std::string;
    auto respond(std::string_view password, std::string_view challenge)
        -> std::expected<std::string, std::string>;
    auto verify(std::string_view final_message) const
        -> std::expected<void, std::string>;
};

/// Uses ICU's complete RFC 4013 SASLprep profile when available. Builds
/// without ICU safely accept printable ASCII credentials only.
auto saslprep(std::string_view input) -> std::expected<std::string, std::string>;

auto scram_initial_response(std::string_view mechanism, std::string_view data)
    -> std::vector<std::uint8_t>;
auto scram_response(std::string_view data) -> std::vector<std::uint8_t>;
auto extended_query_messages(std::string_view statement_name,
    std::string_view sql, std::span<const param_value> params, bool parse)
    -> std::vector<std::uint8_t>;
auto prepare_statement_messages(std::string_view statement_name,
    std::string_view sql) -> std::vector<std::uint8_t>;
auto streaming_portal_start_messages(std::string_view portal_name,
    std::string_view sql, std::uint32_t maximum_rows)
    -> std::vector<std::uint8_t>;
auto streaming_portal_continue_messages(std::string_view portal_name,
    std::uint32_t maximum_rows) -> std::vector<std::uint8_t>;
auto streaming_portal_close_messages(std::string_view portal_name)
    -> std::vector<std::uint8_t>;
auto synchronization_message() -> std::array<std::uint8_t, 5>;
auto count_postgresql_parameters(std::string_view sql) -> std::size_t;
auto decode_text_field(std::uint32_t oid, std::string_view value)
    -> field_value;

} // namespace cnetmod::postgresql::detail
