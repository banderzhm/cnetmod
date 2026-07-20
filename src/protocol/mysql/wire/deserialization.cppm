export module cnetmod.protocol.mysql:wire_deserialization;

import std;
import :types;
import :protocol;

namespace cnetmod::mysql::detail {
struct server_greeting {
  std::uint8_t protocol_version = 0;
  std::string server_version;
  std::uint32_t connection_id = 0;
  std::vector<std::uint8_t> auth_data;
  std::uint32_t capabilities = 0;
  std::uint8_t charset = 0;
  std::uint16_t status_flags = 0;
  std::string auth_plugin_name;
};

struct ok_packet {
  std::uint64_t affected_rows = 0;
  std::uint64_t last_insert_id = 0;
  std::uint16_t status_flags = 0;
  std::uint16_t warnings = 0;
  std::string info;
};

struct err_packet {
  std::uint16_t error_code = 0;
  std::string sql_state;
  std::string message;
};

struct prepare_stmt_response {
  std::uint32_t stmt_id = 0;
  std::uint16_t num_columns = 0;
  std::uint16_t num_params = 0;
  std::uint16_t warnings = 0;
};

enum class handshake_response_type {
  ok,
  error,
  auth_switch,
  auth_more_data,
  unknown
};

struct auth_switch_request {
  std::string plugin_name;
  std::vector<std::uint8_t> auth_data;
};

auto parse_server_greeting(const std::uint8_t *data, std::size_t size)
    -> std::expected<server_greeting, std::string>;
auto parse_ok_packet(const std::uint8_t *data, std::size_t size) -> ok_packet;
auto parse_err_packet(const std::uint8_t *data, std::size_t size) -> err_packet;
auto parse_column_def(const std::uint8_t *data, std::size_t size)
    -> column_meta;
auto parse_text_row(const std::uint8_t *data, std::size_t size,
                    const std::vector<column_meta> &columns) -> row;
auto parse_binary_row(const std::uint8_t *data, std::size_t size,
                      const std::vector<column_meta> &columns) -> row;
auto parse_prepare_stmt_response(const std::uint8_t *data, std::size_t size)
    -> std::expected<prepare_stmt_response, std::string>;
auto classify_handshake_response(const std::uint8_t *data, std::size_t size)
    -> handshake_response_type;
auto parse_auth_switch(const std::uint8_t *data, std::size_t size)
    -> auth_switch_request;
} // namespace cnetmod::mysql::detail