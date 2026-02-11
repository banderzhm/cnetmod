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
#endif

module cnetmod.core.address;

import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// ipv4_address
// =============================================================================

auto ipv4_address::from_string(std::string_view str)
    -> std::expected<ipv4_address, std::error_code>
{
    ipv4_address addr;
    std::string s(str);
    if (::inet_pton(AF_INET, s.c_str(), &addr.addr_) != 1)
        return std::unexpected(make_error_code(errc::invalid_argument));
    return addr;
}

auto ipv4_address::to_string() const -> std::string {
    char buf[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &addr_, buf, sizeof(buf));
    return std::string(buf);
}

// =============================================================================
// ipv6_address
// =============================================================================

auto ipv6_address::from_string(std::string_view str)
    -> std::expected<ipv6_address, std::error_code>
{
    ipv6_address addr;
    std::string s(str);
    if (::inet_pton(AF_INET6, s.c_str(), &addr.addr_) != 1)
        return std::unexpected(make_error_code(errc::invalid_argument));
    return addr;
}

auto ipv6_address::to_string() const -> std::string {
    char buf[INET6_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET6, &addr_, buf, sizeof(buf));
    return std::string(buf);
}

auto ipv6_address::is_loopback() const noexcept -> bool {
    return IN6_IS_ADDR_LOOPBACK(&addr_);
}

auto ipv6_address::loopback() noexcept -> ipv6_address {
    ipv6_address addr;
    addr.addr_ = in6addr_loopback;
    return addr;
}

// =============================================================================
// ip_address
// =============================================================================

auto ip_address::from_string(std::string_view str)
    -> std::expected<ip_address, std::error_code>
{
    // 先尝试 IPv4
    if (auto v4 = ipv4_address::from_string(str))
        return ip_address{*v4};
    // 再尝试 IPv6
    if (auto v6 = ipv6_address::from_string(str))
        return ip_address{*v6};
    return std::unexpected(make_error_code(errc::invalid_argument));
}

auto ip_address::to_string() const -> std::string {
    if (is_v4()) return v4_.to_string();
    return v6_.to_string();
}

// =============================================================================
// endpoint
// =============================================================================

auto endpoint::to_string() const -> std::string {
    if (address_.is_v6())
        return std::format("[{}]:{}", address_.to_string(), port_);
    return std::format("{}:{}", address_.to_string(), port_);
}

} // namespace cnetmod
