module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:error_codes;

import std;

namespace cnetmod::mysql {

// =============================================================================
// client_errc — 客户端错误码（参考 Boost.MySQL client_errc）
// =============================================================================

export enum class client_errc : int {
    /// 收到不完整的消息（反序列化错误）
    incomplete_message = 1,
    /// 服务端消息中发现意外值
    protocol_value_error,
    /// 服务器不支持建立连接所需的最低能力集
    server_unsupported,
    /// 消息末尾有多余字节
    extra_bytes,
    /// 序列号不匹配
    sequence_number_mismatch,
    /// 未知认证插件
    unknown_auth_plugin,
    /// 认证插件要求 SSL
    auth_plugin_requires_ssl,
    /// 传给 prepared statement 的参数数量不匹配
    wrong_num_params,
    /// ssl_mode::require 但服务端不支持 SSL
    server_doesnt_support_ssl,
    /// 静态接口：C++ 类型与服务端返回不匹配
    metadata_check_failed,
    /// 静态接口：结果集数量不匹配
    num_resultsets_mismatch,
    /// 静态接口：行类型不匹配
    row_type_mismatch,
    /// 静态接口：行解析错误
    static_row_parsing_error,
    /// 连接池未运行
    pool_not_running,
    /// 连接池已取消
    pool_cancelled,
    /// 无可用连接
    no_connection_available,
    /// 无效编码
    invalid_encoding,
    /// 无法格式化值
    unformattable_value,
    /// 格式字符串语法错误
    format_string_invalid_syntax,
    /// 格式字符串编码无效
    format_string_invalid_encoding,
    /// 混合使用手动/自动索引
    format_string_manual_auto_mix,
    /// 格式说明符不支持
    format_string_invalid_specifier,
    /// 格式参数未找到
    format_arg_not_found,
    /// 未知字符集
    unknown_character_set,
    /// 超出最大缓冲区
    max_buffer_size_exceeded,
    /// 连接上有另一个操作正在进行
    operation_in_progress,
    /// 操作需要已建立的会话
    not_connected,
    /// 连接正在进行多函数操作
    engaged_in_multi_function,
    /// 操作需要连接处于多函数操作中
    not_engaged_in_multi_function,
    /// 握手包类型错误
    bad_handshake_packet_type,
    /// OpenSSL 未知错误
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
// common_server_errc — 常用 MySQL 服务端错误码
// =============================================================================
//
// 仅包含最常用的错误码。完整列表见 MySQL 文档。
// 数值与 MySQL 服务端定义一致。

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
// is_fatal_error — 判断错误是否需要重连（参考 Boost.MySQL is_fatal_error）
// =============================================================================

/// 判断客户端错误是否致命
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

/// 判断服务端错误码 (uint16) 是否致命（通常是通信类错误）
export inline auto is_fatal_error(std::uint16_t server_error_code) noexcept -> bool {
    // 通信错误 / 连接中断类错误视为致命
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
