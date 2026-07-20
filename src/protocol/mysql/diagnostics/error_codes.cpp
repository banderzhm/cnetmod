module cnetmod.protocol.mysql;
import :error_codes;

namespace cnetmod::mysql {
auto client_errc_to_str(client_errc e) noexcept -> const char * {
  static constexpr const char *labels[] = {"incomplete_message",
                                           "protocol_value_error",
                                           "server_unsupported",
                                           "extra_bytes",
                                           "sequence_number_mismatch",
                                           "unknown_auth_plugin",
                                           "auth_plugin_requires_ssl",
                                           "wrong_num_params",
                                           "server_doesnt_support_ssl",
                                           "metadata_check_failed",
                                           "num_resultsets_mismatch",
                                           "row_type_mismatch",
                                           "static_row_parsing_error",
                                           "pool_not_running",
                                           "pool_cancelled",
                                           "no_connection_available",
                                           "invalid_encoding",
                                           "unformattable_value",
                                           "format_string_invalid_syntax",
                                           "format_string_invalid_encoding",
                                           "format_string_manual_auto_mix",
                                           "format_string_invalid_specifier",
                                           "format_arg_not_found",
                                           "unknown_character_set",
                                           "max_buffer_size_exceeded",
                                           "operation_in_progress",
                                           "not_connected",
                                           "engaged_in_multi_function",
                                           "not_engaged_in_multi_function",
                                           "bad_handshake_packet_type",
                                           "unknown_openssl_error"};
  auto index = static_cast<int>(e) - 1;
  return index >= 0 && index < static_cast<int>(std::size(labels))
             ? labels[index]
             : "<unknown client_errc>";
}

auto common_server_errc_to_str(common_server_errc e) noexcept -> const char * {
  switch (e) {
  case common_server_errc::er_dup_key:
    return "ER_DUP_KEY";
  case common_server_errc::er_access_denied_error:
    return "ER_ACCESS_DENIED_ERROR";
  case common_server_errc::er_bad_db_error:
    return "ER_BAD_DB_ERROR";
  case common_server_errc::er_dup_entry:
    return "ER_DUP_ENTRY";
  case common_server_errc::er_no_such_table:
    return "ER_NO_SUCH_TABLE";
  case common_server_errc::er_too_many_connections:
    return "ER_TOO_MANY_CONNECTIONS";
  case common_server_errc::er_lock_wait_timeout:
    return "ER_LOCK_WAIT_TIMEOUT";
  case common_server_errc::er_lock_deadlock:
    return "ER_LOCK_DEADLOCK";
  default:
    return "<unknown server_errc>";
  }
}

auto is_fatal_error(client_errc e) noexcept -> bool {
  switch (e) {
  case client_errc::incomplete_message:
  case client_errc::protocol_value_error:
  case client_errc::extra_bytes:
  case client_errc::sequence_number_mismatch:
  case client_errc::max_buffer_size_exceeded:
  case client_errc::metadata_check_failed:
  case client_errc::num_resultsets_mismatch:
  case client_errc::row_type_mismatch:
  case client_errc::static_row_parsing_error:
  case client_errc::server_doesnt_support_ssl:
  case client_errc::unknown_auth_plugin:
  case client_errc::server_unsupported:
  case client_errc::auth_plugin_requires_ssl:
  case client_errc::bad_handshake_packet_type:
  case client_errc::unknown_openssl_error:
    return true;
  default:
    return false;
  }
}

auto is_fatal_error(std::uint16_t e) noexcept -> bool {
  switch (static_cast<common_server_errc>(e)) {
  case common_server_errc::er_unknown_com_error:
  case common_server_errc::er_aborting_connection:
  case common_server_errc::er_net_packet_too_large:
  case common_server_errc::er_net_read_error_from_pipe:
  case common_server_errc::er_net_fcntl_error:
  case common_server_errc::er_net_packets_out_of_order:
  case common_server_errc::er_net_uncompress_error:
  case common_server_errc::er_net_read_error:
  case common_server_errc::er_net_read_interrupted:
  case common_server_errc::er_net_error_on_write:
  case common_server_errc::er_net_write_interrupted:
  case common_server_errc::er_malformed_packet:
  case common_server_errc::er_zlib_z_buf_error:
  case common_server_errc::er_zlib_z_data_error:
  case common_server_errc::er_zlib_z_mem_error:
    return true;
  default:
    return false;
  }
}
} // namespace cnetmod::mysql