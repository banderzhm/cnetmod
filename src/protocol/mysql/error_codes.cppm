module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:error_codes;

import std;

namespace cnetmod::mysql {

// =============================================================================
// client_errc — Client error codes (reference: Boost.MySQL client_errc)
// =============================================================================

export enum class client_errc : int {
    /// Received incomplete message (deserialization error)
    incomplete_message = 1,
    /// Unexpected value found in server message
    protocol_value_error,
    /// Server doesn't support minimum capability set required to establish connection
    server_unsupported,
    /// Extra bytes at end of message
    extra_bytes,
    /// Sequence number mismatch
    sequence_number_mismatch,
    /// Unknown authentication plugin
    unknown_auth_plugin,
    /// Authentication plugin requires SSL
    auth_plugin_requires_ssl,
    /// Number of parameters passed to prepared statement doesn't match
    wrong_num_params,
    /// ssl_mode::require but server doesn't support SSL
    server_doesnt_support_ssl,
    /// Static interface: C++ type doesn't match server return
    metadata_check_failed,
    /// Static interface: Number of result sets doesn't match
    num_resultsets_mismatch,
    /// Static interface: Row type mismatch
    row_type_mismatch,
    /// Static interface: Row parsing error
    static_row_parsing_error,
    /// Connection pool not running
    pool_not_running,
    /// Connection pool cancelled
    pool_cancelled,
    /// No connection available
    no_connection_available,
    /// Invalid encoding
    invalid_encoding,
    /// Cannot format value
    unformattable_value,
    /// Format string syntax error
    format_string_invalid_syntax,
    /// Format string encoding invalid
    format_string_invalid_encoding,
    /// Mixed use of manual/auto indexing
    format_string_manual_auto_mix,
    /// Format specifier not supported
    format_string_invalid_specifier,
    /// Format argument not found
    format_arg_not_found,
    /// Unknown character set
    unknown_character_set,
    /// Maximum buffer size exceeded
    max_buffer_size_exceeded,
    /// Another operation is in progress on the connection
    operation_in_progress,
    /// Operation requires an established session
    not_connected,
    /// Connection is engaged in multi-function operation
    engaged_in_multi_function,
    /// Operation requires connection to be in multi-function operation
    not_engaged_in_multi_function,
    /// Bad handshake packet type
    bad_handshake_packet_type,
    /// Unknown OpenSSL error
    unknown_openssl_error,
};

export inline auto client_errc_to_str(client_errc e) noexcept -> const char* {
    switch (e) {
    case client_errc::incomplete_message:              return "incomplete_message";
    case client_errc::protocol_value_error:            return "protocol_value_error";
    case client_errc::server_unsupported:              return "server_unsupported";
    case client_errc::extra_bytes:                     return "extra_bytes";
    case client_errc::sequence_number_mismatch:        return "sequence_number_mismatch";
    case client_errc::unknown_auth_plugin:             return "unknown_auth_plugin";
    case client_errc::auth_plugin_requires_ssl:        return "auth_plugin_requires_ssl";
    case client_errc::wrong_num_params:                return "wrong_num_params";
    case client_errc::server_doesnt_support_ssl:       return "server_doesnt_support_ssl";
    case client_errc::metadata_check_failed:           return "metadata_check_failed";
    case client_errc::num_resultsets_mismatch:         return "num_resultsets_mismatch";
    case client_errc::row_type_mismatch:               return "row_type_mismatch";
    case client_errc::static_row_parsing_error:        return "static_row_parsing_error";
    case client_errc::pool_not_running:                return "pool_not_running";
    case client_errc::pool_cancelled:                  return "pool_cancelled";
    case client_errc::no_connection_available:          return "no_connection_available";
    case client_errc::invalid_encoding:                return "invalid_encoding";
    case client_errc::unformattable_value:             return "unformattable_value";
    case client_errc::format_string_invalid_syntax:    return "format_string_invalid_syntax";
    case client_errc::format_string_invalid_encoding:  return "format_string_invalid_encoding";
    case client_errc::format_string_manual_auto_mix:   return "format_string_manual_auto_mix";
    case client_errc::format_string_invalid_specifier: return "format_string_invalid_specifier";
    case client_errc::format_arg_not_found:            return "format_arg_not_found";
    case client_errc::unknown_character_set:            return "unknown_character_set";
    case client_errc::max_buffer_size_exceeded:         return "max_buffer_size_exceeded";
    case client_errc::operation_in_progress:            return "operation_in_progress";
    case client_errc::not_connected:                   return "not_connected";
    case client_errc::engaged_in_multi_function:       return "engaged_in_multi_function";
    case client_errc::not_engaged_in_multi_function:   return "not_engaged_in_multi_function";
    case client_errc::bad_handshake_packet_type:       return "bad_handshake_packet_type";
    case client_errc::unknown_openssl_error:           return "unknown_openssl_error";
    default:                                           return "<unknown client_errc>";
    }
}

// =============================================================================
// common_server_errc — Common MySQL server error codes
// =============================================================================
//
// Only includes most commonly used error codes. See MySQL documentation for complete list.
// Values match MySQL server definitions.

export enum class common_server_errc : int {
    er_dup_key                     = 1022,
    er_access_denied_error         = 1045,
    er_bad_db_error                = 1049,
    er_table_exists_error          = 1050,
    er_bad_table_error             = 1051,
    er_non_uniq_error              = 1052,
    er_key_not_found               = 1032,
    er_dup_entry                   = 1062,
    er_parse_error                 = 1064,
    er_empty_query                 = 1065,
    er_no_such_table               = 1146,
    er_syntax_error                = 1149,
    er_aborting_connection         = 1152,
    er_net_packets_out_of_order    = 1156,
    er_net_read_error_from_pipe    = 1154,
    er_net_read_error              = 1158,
    er_net_read_interrupted        = 1159,
    er_net_error_on_write          = 1160,
    er_net_write_interrupted       = 1161,
    er_net_packet_too_large        = 1153,
    er_net_fcntl_error             = 1155,
    er_net_uncompress_error        = 1157,
    er_unknown_com_error           = 1047,
    er_malformed_packet            = 1835,
    er_server_shutdown             = 1053,
    er_too_many_connections        = 1040,
    er_con_count_error             = 1041,
    er_lock_wait_timeout           = 1205,
    er_lock_deadlock               = 1213,
    er_data_too_long               = 1406,
    er_constraint_failed           = 4025,
    er_zlib_z_buf_error            = 3057,
    er_zlib_z_data_error           = 3058,
    er_zlib_z_mem_error            = 3059,
};

export inline auto common_server_errc_to_str(common_server_errc e) noexcept -> const char* {
    switch (e) {
    case common_server_errc::er_dup_key:                  return "ER_DUP_KEY";
    case common_server_errc::er_access_denied_error:      return "ER_ACCESS_DENIED_ERROR";
    case common_server_errc::er_bad_db_error:             return "ER_BAD_DB_ERROR";
    case common_server_errc::er_table_exists_error:       return "ER_TABLE_EXISTS_ERROR";
    case common_server_errc::er_bad_table_error:          return "ER_BAD_TABLE_ERROR";
    case common_server_errc::er_non_uniq_error:           return "ER_NON_UNIQ_ERROR";
    case common_server_errc::er_key_not_found:            return "ER_KEY_NOT_FOUND";
    case common_server_errc::er_dup_entry:                return "ER_DUP_ENTRY";
    case common_server_errc::er_parse_error:              return "ER_PARSE_ERROR";
    case common_server_errc::er_empty_query:              return "ER_EMPTY_QUERY";
    case common_server_errc::er_no_such_table:            return "ER_NO_SUCH_TABLE";
    case common_server_errc::er_syntax_error:             return "ER_SYNTAX_ERROR";
    case common_server_errc::er_aborting_connection:       return "ER_ABORTING_CONNECTION";
    case common_server_errc::er_net_packets_out_of_order:  return "ER_NET_PACKETS_OUT_OF_ORDER";
    case common_server_errc::er_net_read_error:            return "ER_NET_READ_ERROR";
    case common_server_errc::er_net_error_on_write:        return "ER_NET_ERROR_ON_WRITE";
    case common_server_errc::er_net_packet_too_large:      return "ER_NET_PACKET_TOO_LARGE";
    case common_server_errc::er_unknown_com_error:         return "ER_UNKNOWN_COM_ERROR";
    case common_server_errc::er_malformed_packet:          return "ER_MALFORMED_PACKET";
    case common_server_errc::er_server_shutdown:            return "ER_SERVER_SHUTDOWN";
    case common_server_errc::er_too_many_connections:       return "ER_TOO_MANY_CONNECTIONS";
    case common_server_errc::er_lock_wait_timeout:         return "ER_LOCK_WAIT_TIMEOUT";
    case common_server_errc::er_lock_deadlock:             return "ER_LOCK_DEADLOCK";
    case common_server_errc::er_data_too_long:             return "ER_DATA_TOO_LONG";
    default:                                               return "<unknown server_errc>";
    }
}

// =============================================================================
// is_fatal_error — Determine if error requires reconnection (reference: Boost.MySQL is_fatal_error)
// =============================================================================

/// Determine if client error is fatal
export inline auto is_fatal_error(client_errc ec) noexcept -> bool {
    switch (ec) {
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

/// Determine if server error code (uint16) is fatal (usually communication errors)
export inline auto is_fatal_error(std::uint16_t server_error_code) noexcept -> bool {
    // Communication errors / connection interruption errors are considered fatal
    switch (static_cast<common_server_errc>(server_error_code)) {
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
