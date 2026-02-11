module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif

module cnetmod.core.socket;

import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// 辅助：address_family -> AF_xxx / socket_type -> SOCK_xxx
// =============================================================================

namespace {

auto to_native_family(address_family family) noexcept -> int {
    switch (family) {
        case address_family::ipv4:        return AF_INET;
        case address_family::ipv6:        return AF_INET6;
        case address_family::unspecified: return AF_UNSPEC;
    }
    return AF_UNSPEC;
}

auto to_native_socktype(socket_type type) noexcept -> int {
    switch (type) {
        case socket_type::stream:   return SOCK_STREAM;
        case socket_type::datagram: return SOCK_DGRAM;
    }
    return SOCK_STREAM;
}

auto last_error() noexcept -> int {
#ifdef CNETMOD_PLATFORM_WINDOWS
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

/// 填充 sockaddr_storage，返回长度
auto fill_sockaddr(const endpoint& ep, ::sockaddr_storage& storage) noexcept -> int {
    std::memset(&storage, 0, sizeof(storage));
    if (ep.address().is_v4()) {
        auto& sa = reinterpret_cast<::sockaddr_in&>(storage);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(ep.port());
        sa.sin_addr = ep.address().to_v4().native();
        return static_cast<int>(sizeof(::sockaddr_in));
    } else {
        auto& sa = reinterpret_cast<::sockaddr_in6&>(storage);
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(ep.port());
        sa.sin6_addr = ep.address().to_v6().native();
        return static_cast<int>(sizeof(::sockaddr_in6));
    }
}

} // anonymous namespace

// =============================================================================
// 生命周期
// =============================================================================

socket::~socket() {
    close();
}

socket::socket(socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = invalid_handle;
}

auto socket::operator=(socket&& other) noexcept -> socket& {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = invalid_handle;
    }
    return *this;
}

// =============================================================================
// 创建
// =============================================================================

auto socket::create(address_family family, socket_type type)
    -> std::expected<socket, std::error_code>
{
    int af = to_native_family(family);
    int st = to_native_socktype(type);
    int proto = (type == socket_type::stream) ? IPPROTO_TCP : IPPROTO_UDP;

#ifdef CNETMOD_PLATFORM_WINDOWS
    // WSA_FLAG_OVERLAPPED 使得句柄可关联 IOCP
    SOCKET fd = ::WSASocketW(af, st, proto, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#else
    int fd = ::socket(af, st, proto);
    if (fd < 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#endif

    return socket{fd};
}

// =============================================================================
// bind / listen
// =============================================================================

auto socket::bind(const endpoint& ep) -> std::expected<void, std::error_code> {
    ::sockaddr_storage storage{};
    int len = fill_sockaddr(ep, storage);

    if (::bind(handle_, reinterpret_cast<const ::sockaddr*>(&storage), len) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));

    return {};
}

auto socket::listen(int backlog) -> std::expected<void, std::error_code> {
    if (::listen(handle_, backlog) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
    return {};
}

// =============================================================================
// 选项
// =============================================================================

auto socket::set_non_blocking(bool enabled) -> std::expected<void, std::error_code> {
#ifdef CNETMOD_PLATFORM_WINDOWS
    u_long mode = enabled ? 1 : 0;
    if (::ioctlsocket(handle_, FIONBIO, &mode) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#else
    int flags = ::fcntl(handle_, F_GETFL, 0);
    if (flags < 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
    flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(handle_, F_SETFL, flags) < 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#endif
    return {};
}

auto socket::apply_options(const socket_options& opts)
    -> std::expected<void, std::error_code>
{
    // SO_REUSEADDR
    if (opts.reuse_address) {
        int val = 1;
        if (::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR,
                         reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    // SO_REUSEPORT (仅 POSIX)
#ifndef CNETMOD_PLATFORM_WINDOWS
    if (opts.reuse_port) {
        int val = 1;
        if (::setsockopt(handle_, SOL_SOCKET, SO_REUSEPORT,
                         &val, sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }
#endif

    // TCP_NODELAY
    if (opts.no_delay) {
        int val = 1;
        if (::setsockopt(handle_, IPPROTO_TCP, TCP_NODELAY,
                         reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    // 非阻塞
    if (opts.non_blocking) {
        if (auto r = set_non_blocking(true); !r) return r;
    }

    // 接收缓冲区
    if (opts.recv_buffer_size > 0) {
        int val = opts.recv_buffer_size;
        if (::setsockopt(handle_, SOL_SOCKET, SO_RCVBUF,
                         reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    // 发送缓冲区
    if (opts.send_buffer_size > 0) {
        int val = opts.send_buffer_size;
        if (::setsockopt(handle_, SOL_SOCKET, SO_SNDBUF,
                         reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    return {};
}

// =============================================================================
// 关闭
// =============================================================================

void socket::close() noexcept {
    if (handle_ == invalid_handle) return;
#ifdef CNETMOD_PLATFORM_WINDOWS
    ::closesocket(handle_);
#else
    ::close(handle_);
#endif
    handle_ = invalid_handle;
}

} // namespace cnetmod
