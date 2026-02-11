module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#endif

export module cnetmod.core.serial_port;

import std;
import cnetmod.core.error;
import cnetmod.core.file;  // file_handle_t, invalid_file_handle

namespace cnetmod {

// =============================================================================
// 串口配置枚举
// =============================================================================

export enum class parity : std::uint8_t {
    none,
    odd,
    even,
    mark,
    space,
};

export enum class stop_bits : std::uint8_t {
    one,        // 1 停止位
    one_half,   // 1.5 停止位
    two,        // 2 停止位
};

export enum class flow_control : std::uint8_t {
    none,
    hardware,   // RTS/CTS
    software,   // XON/XOFF
};

// =============================================================================
// 串口配置
// =============================================================================

export struct serial_config {
    std::uint32_t  baud_rate     = 9600;
    std::uint8_t   data_bits     = 8;       // 5, 6, 7, 8
    stop_bits      stop          = stop_bits::one;
    parity         par           = parity::none;
    flow_control   flow          = flow_control::none;

    // 读超时 (毫秒, 0 = 不超时)
    std::uint32_t  read_timeout_ms  = 1000;
    // 写超时 (毫秒, 0 = 不超时)
    std::uint32_t  write_timeout_ms = 1000;
};

// =============================================================================
// Serial Port 类
// =============================================================================

/// 平台无关的串口句柄封装（RAII）
/// 支持异步 I/O（通过 io_context 驱动）
export class serial_port {
public:
    serial_port() noexcept = default;
    ~serial_port();

    // 不可复制
    serial_port(const serial_port&) = delete;
    auto operator=(const serial_port&) -> serial_port& = delete;

    // 可移动
    serial_port(serial_port&& other) noexcept;
    auto operator=(serial_port&& other) noexcept -> serial_port&;

    /// 打开串口
    /// Windows: name = "COM3", "COM4", ...
    /// Linux:   name = "/dev/ttyUSB0", "/dev/ttyS0", ...
    [[nodiscard]] static auto open(
        std::string_view name,
        const serial_config& config = {}
    ) -> std::expected<serial_port, std::error_code>;

    /// 关闭串口
    void close() noexcept;

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

    /// 是否已打开
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return handle_ != invalid_file_handle;
    }

    explicit operator bool() const noexcept { return is_open(); }

    /// 获取当前配置
    [[nodiscard]] auto config() const noexcept -> const serial_config& {
        return config_;
    }

private:
    explicit serial_port(file_handle_t handle, serial_config cfg) noexcept
        : handle_(handle), config_(std::move(cfg)) {}

    file_handle_t handle_ = invalid_file_handle;
    serial_config config_{};
};

} // namespace cnetmod
