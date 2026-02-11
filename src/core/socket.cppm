module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#endif

export module cnetmod.core.socket;

import std;
import cnetmod.core.error;
import cnetmod.core.address;

namespace cnetmod {

// =============================================================================
// 平台类型别名
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
export using native_handle_t = SOCKET;
export inline constexpr native_handle_t invalid_handle = INVALID_SOCKET;
#else
export using native_handle_t = int;
export inline constexpr native_handle_t invalid_handle = -1;
#endif

// =============================================================================
// 套接字类型
// =============================================================================

/// 套接字协议类型
export enum class socket_type {
    stream,    // TCP
    datagram,  // UDP
};

// =============================================================================
// 套接字选项
// =============================================================================

/// 套接字选项
export struct socket_options {
    bool reuse_address = false;
    bool reuse_port = false;
    bool non_blocking = true;
    bool no_delay = false;        // TCP_NODELAY
    int recv_buffer_size = 0;     // 0 = 系统默认
    int send_buffer_size = 0;     // 0 = 系统默认
};

// =============================================================================
// Socket 类
// =============================================================================

/// 平台无关的套接字封装
/// 拥有 native handle 的生命周期（RAII）
export class socket {
public:
    socket() noexcept = default;
    ~socket();

    // 不可复制
    socket(const socket&) = delete;
    auto operator=(const socket&) -> socket& = delete;

    // 可移动
    socket(socket&& other) noexcept;
    auto operator=(socket&& other) noexcept -> socket&;

    /// 创建套接字
    [[nodiscard]] static auto create(
        address_family family,
        socket_type type
    ) -> std::expected<socket, std::error_code>;

    /// 从原生句柄构造（接管所有权）
    [[nodiscard]] static auto from_native(native_handle_t handle) noexcept -> socket {
        return socket{handle};
    }

    /// 绑定地址
    [[nodiscard]] auto bind(const endpoint& ep) -> std::expected<void, std::error_code>;

    /// 监听
    [[nodiscard]] auto listen(int backlog = 128) -> std::expected<void, std::error_code>;

    /// 设置非阻塞模式
    [[nodiscard]] auto set_non_blocking(bool enabled) -> std::expected<void, std::error_code>;

    /// 应用选项
    [[nodiscard]] auto apply_options(const socket_options& opts) -> std::expected<void, std::error_code>;

    /// 关闭套接字
    void close() noexcept;

    /// 获取原生句柄
    [[nodiscard]] auto native_handle() const noexcept -> native_handle_t {
        return handle_;
    }

    /// 释放所有权（不关闭）
    [[nodiscard]] auto release() noexcept -> native_handle_t {
        auto h = handle_;
        handle_ = invalid_handle;
        return h;
    }

    /// 是否有效
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return handle_ != invalid_handle;
    }

    explicit operator bool() const noexcept { return is_open(); }

private:
    explicit socket(native_handle_t handle) noexcept : handle_(handle) {}

    native_handle_t handle_ = invalid_handle;
};

} // namespace cnetmod
