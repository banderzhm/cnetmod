module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <WinSock2.h>
#endif

export module cnetmod.core.error;

import std;

namespace cnetmod {

// =============================================================================
// 错误码枚举
// =============================================================================

/// 网络操作错误码
export enum class errc {
    success = 0,

    // 连接相关
    connection_refused,
    connection_reset,
    connection_aborted,
    connection_timed_out,
    not_connected,
    already_connected,

    // 地址相关
    address_in_use,
    address_not_available,
    address_family_not_supported,

    // 操作相关
    operation_aborted,
    operation_in_progress,
    operation_not_supported,
    operation_would_block,

    // 资源相关
    too_many_files_open,
    no_buffer_space,
    out_of_memory,

    // 网络相关
    network_down,
    network_unreachable,
    host_unreachable,
    host_not_found,

    // I/O 相关
    broken_pipe,
    end_of_file,
    bad_descriptor,

    // 通用
    permission_denied,
    invalid_argument,
    unknown_error,
};

// =============================================================================
// error_category
// =============================================================================

/// cnetmod 错误类别
export class network_error_category : public std::error_category {
public:
    [[nodiscard]] auto name() const noexcept -> const char* override {
        return "cnetmod";
    }

    [[nodiscard]] auto message(int ev) const -> std::string override {
        switch (static_cast<errc>(ev)) {
            case errc::success:                    return "success";
            case errc::connection_refused:          return "connection refused";
            case errc::connection_reset:            return "connection reset";
            case errc::connection_aborted:          return "connection aborted";
            case errc::connection_timed_out:        return "connection timed out";
            case errc::not_connected:               return "not connected";
            case errc::already_connected:           return "already connected";
            case errc::address_in_use:              return "address in use";
            case errc::address_not_available:       return "address not available";
            case errc::address_family_not_supported: return "address family not supported";
            case errc::operation_aborted:           return "operation aborted";
            case errc::operation_in_progress:       return "operation in progress";
            case errc::operation_not_supported:     return "operation not supported";
            case errc::operation_would_block:       return "operation would block";
            case errc::too_many_files_open:         return "too many files open";
            case errc::no_buffer_space:             return "no buffer space";
            case errc::out_of_memory:               return "out of memory";
            case errc::network_down:                return "network down";
            case errc::network_unreachable:         return "network unreachable";
            case errc::host_unreachable:            return "host unreachable";
            case errc::host_not_found:              return "host not found";
            case errc::broken_pipe:                 return "broken pipe";
            case errc::end_of_file:                 return "end of file";
            case errc::bad_descriptor:              return "bad descriptor";
            case errc::permission_denied:           return "permission denied";
            case errc::invalid_argument:            return "invalid argument";
            case errc::unknown_error:               return "unknown error";
            default:                                return "unrecognized error";
        }
    }
};

/// 获取全局 network_error_category 实例
export [[nodiscard]] inline auto network_category() noexcept
    -> const std::error_category&
{
    static const network_error_category instance;
    return instance;
}

/// 创建 std::error_code
export [[nodiscard]] inline auto make_error_code(errc e) noexcept
    -> std::error_code
{
    return {static_cast<int>(e), network_category()};
}

/// 将平台原生错误码转换为 cnetmod::errc
export [[nodiscard]] auto from_native_error(int native_error) noexcept -> errc;

} // namespace cnetmod

// 注册到 std::error_code 系统
template <>
struct std::is_error_code_enum<cnetmod::errc> : std::true_type {};
