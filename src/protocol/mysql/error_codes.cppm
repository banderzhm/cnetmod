export module cnetmod.protocol.mysql:error_codes;
import std;
export namespace cnetmod::mysql {
enum class client_errc : int {
  incomplete_message = 1,
  protocol_value_error,
  server_unsupported,
  extra_bytes,
  sequence_number_mismatch,
  unknown_auth_plugin,
  auth_plugin_requires_ssl,
  wrong_num_params,
  server_doesnt_support_ssl,
  metadata_check_failed,
  num_resultsets_mismatch,
  row_type_mismatch,
  static_row_parsing_error,
  pool_not_running,
  pool_cancelled,
  no_connection_available,
  invalid_encoding,
  unformattable_value,
  format_string_invalid_syntax,
  format_string_invalid_encoding,
  format_string_manual_auto_mix,
  format_string_invalid_specifier,
  format_arg_not_found,
  unknown_character_set,
  max_buffer_size_exceeded,
  operation_in_progress,
  not_connected,
  engaged_in_multi_function,
  not_engaged_in_multi_function,
  bad_handshake_packet_type,
  unknown_openssl_error
};

auto client_errc_to_str(client_errc) noexcept -> const char *;

enum class common_server_errc : int {
  er_dup_key = 1022,
  er_access_denied_error = 1045,
  er_bad_db_error = 1049,
  er_table_exists_error = 1050,
  er_bad_table_error = 1051,
  er_non_uniq_error = 1052,
  er_key_not_found = 1032,
  er_dup_entry = 1062,
  er_parse_error = 1064,
  er_empty_query = 1065,
  er_no_such_table = 1146,
  er_syntax_error = 1149,
  er_aborting_connection = 1152,
  er_net_packets_out_of_order = 1156,
  er_net_read_error_from_pipe = 1154,
  er_net_read_error = 1158,
  er_net_read_interrupted = 1159,
  er_net_error_on_write = 1160,
  er_net_write_interrupted = 1161,
  er_net_packet_too_large = 1153,
  er_net_fcntl_error = 1155,
  er_net_uncompress_error = 1157,
  er_unknown_com_error = 1047,
  er_malformed_packet = 1835,
  er_server_shutdown = 1053,
  er_too_many_connections = 1040,
  er_con_count_error = 1041,
  er_lock_wait_timeout = 1205,
  er_lock_deadlock = 1213,
  er_data_too_long = 1406,
  er_constraint_failed = 4025,
  er_zlib_z_buf_error = 3057,
  er_zlib_z_data_error = 3058,
  er_zlib_z_mem_error = 3059
};

auto common_server_errc_to_str(common_server_errc) noexcept -> const char *;
auto is_fatal_error(client_errc) noexcept -> bool;
auto is_fatal_error(std::uint16_t) noexcept -> bool;
} // namespace cnetmod::mysql