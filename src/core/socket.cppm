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
// Platform Type Aliases
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
export using native_handle_t = SOCKET;
export inline constexpr native_handle_t invalid_handle = INVALID_SOCKET;
#else
export using native_handle_t = int;
export inline constexpr native_handle_t invalid_handle = -1;
#endif

// =============================================================================
// Socket Type
// =============================================================================

/// Socket protocol type
export enum class socket_type {
    stream,    // TCP
    datagram,  // UDP
};

// =============================================================================
// Socket Options
// =============================================================================

/// Socket options
export struct socket_options {
    bool reuse_address = false;
    bool reuse_port = false;
    bool non_blocking = true;
    bool no_delay = false;        // TCP_NODELAY
    int recv_buffer_size = 0;     // 0 = system default
    int send_buffer_size = 0;     // 0 = system default
};

// =============================================================================
// Socket Class
// =============================================================================

/// Platform-independent socket wrapper
/// Owns the lifetime of native handle (RAII)
export class socket {
public:
    socket() noexcept = default;
    ~socket();

    // Non-copyable
    socket(const socket&) = delete;
    auto operator=(const socket&) -> socket& = delete;

    // Movable
    socket(socket&& other) noexcept;
    auto operator=(socket&& other) noexcept -> socket&;

    /// Create socket
    [[nodiscard]] static auto create(
        address_family family,
        socket_type type
    ) -> std::expected<socket, std::error_code>;

    /// Construct from native handle (takes ownership)
    [[nodiscard]] static auto from_native(native_handle_t handle) noexcept -> socket {
        return socket{handle};
    }

    /// Bind address
    [[nodiscard]] auto bind(const endpoint& ep) -> std::expected<void, std::error_code>;

    /// Listen
    [[nodiscard]] auto listen(int backlog = 128) -> std::expected<void, std::error_code>;

    /// Set non-blocking mode
    [[nodiscard]] auto set_non_blocking(bool enabled) -> std::expected<void, std::error_code>;

    /// Apply options
    [[nodiscard]] auto apply_options(const socket_options& opts) -> std::expected<void, std::error_code>;

    /// Get local endpoint (getsockname)
    [[nodiscard]] auto local_endpoint() const -> std::expected<endpoint, std::error_code>;

    /// Close socket
    void close() noexcept;

    /// Get native handle
    [[nodiscard]] auto native_handle() const noexcept -> native_handle_t {
        return handle_;
    }

    /// Release ownership (does not close)
    [[nodiscard]] auto release() noexcept -> native_handle_t {
        auto h = handle_;
        handle_ = invalid_handle;
        return h;
    }

    /// Check if valid
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return handle_ != invalid_handle;
    }

    explicit operator bool() const noexcept { return is_open(); }

private:
    explicit socket(native_handle_t handle) noexcept : handle_(handle) {}

    native_handle_t handle_ = invalid_handle;
};

} // namespace cnetmod
