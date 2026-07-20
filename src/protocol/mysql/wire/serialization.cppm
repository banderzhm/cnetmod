export module cnetmod.protocol.mysql:wire_serialization;

import std;
import :types;
import :protocol;
import :wire_deserialization;

namespace cnetmod::mysql::detail {
auto build_login_packet(const connect_options &opts,
                        const server_greeting &greeting,
                        std::uint32_t client_caps,
                        const std::vector<std::uint8_t> &auth_response)
    -> std::vector<std::uint8_t>;

auto build_ssl_request(std::uint32_t client_caps) -> std::vector<std::uint8_t>;
auto build_prepare_stmt_command(std::string_view sql)
    -> std::vector<std::uint8_t>;
auto build_execute_stmt_command(std::uint32_t stmt_id,
                                std::span<const param_value> params)
    -> std::vector<std::uint8_t>;
auto build_close_stmt_command(std::uint32_t stmt_id)
    -> std::vector<std::uint8_t>;
auto build_query_command(std::string_view sql) -> std::vector<std::uint8_t>;
} // namespace cnetmod::mysql::detail