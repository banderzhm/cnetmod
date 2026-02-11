module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#endif

module cnetmod.protocol.tcp;

namespace cnetmod::tcp {

// =============================================================================
// acceptor
// =============================================================================

auto acceptor::open(const endpoint& ep, const socket_options& opts)
    -> std::expected<void, std::error_code>
{
    auto family = ep.address().is_v6() ? address_family::ipv6 : address_family::ipv4;
    auto sock = socket::create(family, socket_type::stream);
    if (!sock) return std::unexpected(sock.error());

    if (auto r = sock->apply_options(opts); !r) return r;
    if (auto r = sock->bind(ep); !r) return r;
    if (auto r = sock->listen(); !r) return r;

    socket_ = std::move(*sock);
    return {};
}

// =============================================================================
// connection — 端点查询辅助
// =============================================================================

namespace {

auto sockaddr_to_endpoint(const ::sockaddr_storage& storage) -> endpoint {
    if (storage.ss_family == AF_INET) {
        auto& sa = reinterpret_cast<const ::sockaddr_in&>(storage);
        char buf[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        auto a = ipv4_address::from_string(buf);
        return endpoint{ip_address{a.value_or(ipv4_address{})}, ntohs(sa.sin_port)};
    } else {
        auto& sa = reinterpret_cast<const ::sockaddr_in6&>(storage);
        char buf[INET6_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET6, &sa.sin6_addr, buf, sizeof(buf));
        auto a = ipv6_address::from_string(buf);
        return endpoint{ip_address{a.value_or(ipv6_address{})}, ntohs(sa.sin6_port)};
    }
}

} // anonymous namespace

auto connection::remote_endpoint() const
    -> std::expected<endpoint, std::error_code>
{
    ::sockaddr_storage storage{};
#ifdef CNETMOD_PLATFORM_WINDOWS
    int len = sizeof(storage);
#else
    ::socklen_t len = sizeof(storage);
#endif
    if (::getpeername(static_cast<int>(socket_.native_handle()),
            reinterpret_cast<::sockaddr*>(&storage), &len) != 0) {
#ifdef CNETMOD_PLATFORM_WINDOWS
        return std::unexpected(make_error_code(from_native_error(::WSAGetLastError())));
#else
        return std::unexpected(make_error_code(from_native_error(errno)));
#endif
    }
    return sockaddr_to_endpoint(storage);
}

auto connection::local_endpoint() const
    -> std::expected<endpoint, std::error_code>
{
    ::sockaddr_storage storage{};
#ifdef CNETMOD_PLATFORM_WINDOWS
    int len = sizeof(storage);
#else
    ::socklen_t len = sizeof(storage);
#endif
    if (::getsockname(static_cast<int>(socket_.native_handle()),
            reinterpret_cast<::sockaddr*>(&storage), &len) != 0) {
#ifdef CNETMOD_PLATFORM_WINDOWS
        return std::unexpected(make_error_code(from_native_error(::WSAGetLastError())));
#else
        return std::unexpected(make_error_code(from_native_error(errno)));
#endif
    }
    return sockaddr_to_endpoint(storage);
}

} // namespace cnetmod::tcp
