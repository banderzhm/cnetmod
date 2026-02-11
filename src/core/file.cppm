module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

export module cnetmod.core.file;

import std;
import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// 平台类型别名
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
export using file_handle_t = HANDLE;
export inline const file_handle_t invalid_file_handle = INVALID_HANDLE_VALUE;
#else
export using file_handle_t = int;
export inline constexpr file_handle_t invalid_file_handle = -1;
#endif

// =============================================================================
// 文件打开模式
// =============================================================================

export enum class open_mode : std::uint32_t {
    read         = 0x01,
    write        = 0x02,
    read_write   = 0x03,
    append       = 0x04,
    create       = 0x08,   // 不存在则创建
    truncate     = 0x10,   // 存在则截断
    create_new   = 0x20,   // 必须不存在
};

export constexpr auto operator|(open_mode a, open_mode b) noexcept -> open_mode {
    return static_cast<open_mode>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

export constexpr auto operator&(open_mode a, open_mode b) noexcept -> open_mode {
    return static_cast<open_mode>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

export constexpr auto has_flag(open_mode mode, open_mode flag) noexcept -> bool {
    return (mode & flag) == flag;
}

// =============================================================================
// File 类
// =============================================================================

/// 平台无关的文件句柄封装（RAII）
/// 支持异步 I/O（通过 io_context 驱动）
export class file {
public:
    file() noexcept = default;
    ~file();

    // 不可复制
    file(const file&) = delete;
    auto operator=(const file&) -> file& = delete;

    // 可移动
    file(file&& other) noexcept;
    auto operator=(file&& other) noexcept -> file&;

    /// 打开文件
    [[nodiscard]] static auto open(
        const std::filesystem::path& path,
        open_mode mode
    ) -> std::expected<file, std::error_code>;

    /// 关闭文件
    void close() noexcept;

    /// 获取文件大小
    [[nodiscard]] auto size() const -> std::expected<std::uint64_t, std::error_code>;

    /// 获取原生句柄
    [[nodiscard]] auto native_handle() const noexcept -> file_handle_t {
        return handle_;
    }

    /// 释放所有权（不关闭）
    [[nodiscard]] auto release() noexcept -> file_handle_t {
        auto h = handle_;
        handle_ = invalid_file_handle;
        return h;
    }

    /// 是否有效
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return handle_ != invalid_file_handle;
    }

    explicit operator bool() const noexcept { return is_open(); }

private:
    explicit file(file_handle_t handle) noexcept : handle_(handle) {}

    file_handle_t handle_ = invalid_file_handle;
};

} // namespace cnetmod
