module;

#include <cnetmod/config.hpp>
#include <cstring>

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

/**
 * @file ip_filter.cppm
 * @brief IP whitelist/blacklist middleware — access control based on client IP
 *
 * Supports whitelist mode (only allow IPs in list) and blacklist mode (deny IPs in list).
 * Automatically parses X-Forwarded-For / X-Real-IP to get real client IP.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.ip_filter;
 *
 *   // Whitelist: only allow internal network
 *   svr.use(ip_filter({.allow_list = {"127.0.0.1", "10.0.0.0/8", "2001:db8::/32"}}));
 *
 *   // Blacklist: ban specific IPs
 *   svr.use(ip_filter({.deny_list = {"1.2.3.4", "5.6.7.8", "::1"}}));
 *
 *   // Trust proxy headers only when the socket peer is a known reverse proxy
 *   svr.use(ip_filter({.allow_list = {"10.0.0.0/8"}, .trusted_proxies = {"127.0.0.1", "::1"}}));
 */
export module cnetmod.protocol.http.middleware.ip_filter;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// ip_filter_options — IP filter configuration
// =============================================================================

export struct ip_filter_options {
    /// Whitelist: when non-empty, only allow IPs in list (takes priority over blacklist)
    std::vector<std::string> allow_list;

    /// Blacklist: deny IPs in list (only effective when whitelist is empty)
    std::vector<std::string> deny_list;

    /// Trusted reverse proxies allowed to supply X-Forwarded-For / X-Real-IP.
    /// Empty keeps legacy behavior and trusts these headers from any peer.
    std::vector<std::string> trusted_proxies;

    /// HTTP status code when denied
    int denied_status = http::status::forbidden;
};

// =============================================================================
// Internal: IP parsing and matching
// =============================================================================

namespace detail {

inline auto trim(std::string_view s) noexcept -> std::string_view {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

inline auto strip_ipv6_brackets(std::string_view s) noexcept -> std::string_view {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        return s.substr(1, s.size() - 2);
    return s;
}

template <std::size_t N>
inline auto parse_addr(std::string_view s, int family)
    -> std::optional<std::array<std::uint8_t, N>>
{
    s = strip_ipv6_brackets(s);
    std::array<std::uint8_t, N> out{};
    std::string tmp{s};
    if (::inet_pton(family, tmp.c_str(), out.data()) != 1)
        return std::nullopt;
    return out;
}

inline auto parse_prefix(std::string_view bits_str, int max_bits) -> std::optional<int> {
    bits_str = trim(bits_str);
    if (bits_str.empty())
        return std::nullopt;

    int bits = 0;
    auto [ptr, ec] = std::from_chars(bits_str.data(),
                                     bits_str.data() + bits_str.size(),
                                     bits);
    if (ec != std::errc{} || ptr != bits_str.data() + bits_str.size())
        return std::nullopt;
    if (bits < 0 || bits > max_bits)
        return std::nullopt;
    return bits;
}

template <std::size_t N>
inline auto prefix_matches(const std::array<std::uint8_t, N>& client,
                           const std::array<std::uint8_t, N>& network,
                           int prefix_bits) noexcept -> bool
{
    const auto full_bytes = static_cast<std::size_t>(prefix_bits / 8);
    const auto rem_bits = prefix_bits % 8;

    for (std::size_t i = 0; i < full_bytes; ++i)
        if (client[i] != network[i])
            return false;

    if (rem_bits == 0)
        return true;

    const auto mask = static_cast<std::uint8_t>(0xffu << (8 - rem_bits));
    return (client[full_bytes] & mask) == (network[full_bytes] & mask);
}

/// CIDR/exact matching for IPv4 and IPv6.
/// Format: "10.0.0.0/8", "2001:db8::/32", exact "192.168.1.1" or "::1".
inline auto ip_matches(std::string_view client_ip,
                       std::string_view pattern) -> bool
{
    client_ip = strip_ipv6_brackets(client_ip);
    pattern = strip_ipv6_brackets(pattern);

    // Exact match
    if (client_ip == pattern)
        return true;

    // CIDR match
    auto slash = pattern.find('/');
    if (slash == std::string_view::npos) {
        auto client4 = parse_addr<4>(client_ip, AF_INET);
        auto pattern4 = parse_addr<4>(pattern, AF_INET);
        if (client4 && pattern4)
            return *client4 == *pattern4;

        auto client6 = parse_addr<16>(client_ip, AF_INET6);
        auto pattern6 = parse_addr<16>(pattern, AF_INET6);
        return client6 && pattern6 && *client6 == *pattern6;
    }

    auto net_str = strip_ipv6_brackets(pattern.substr(0, slash));
    auto bits_str = pattern.substr(slash + 1);

    if (auto client4 = parse_addr<4>(client_ip, AF_INET)) {
        auto network4 = parse_addr<4>(net_str, AF_INET);
        auto bits = parse_prefix(bits_str, 32);
        return network4 && bits && prefix_matches(*client4, *network4, *bits);
    }

    if (auto client6 = parse_addr<16>(client_ip, AF_INET6)) {
        auto network6 = parse_addr<16>(net_str, AF_INET6);
        auto bits = parse_prefix(bits_str, 128);
        return network6 && bits && prefix_matches(*client6, *network6, *bits);
    }

    return false;
}

/// Check if IP is in list (supports CIDR)
inline auto ip_in_list(std::string_view ip,
                       const std::vector<std::string>& list) -> bool
{
    for (auto& pattern : list)
        if (ip_matches(ip, pattern)) return true;
    return false;
}

inline auto socket_peer_ip(http::request_context& ctx) -> std::string {
    if (auto ep = ctx.raw_socket().remote_endpoint()) {
        return ep->address().to_string();
    }
    return "unknown";
}

inline auto resolve_client_ip(http::request_context& ctx,
                              const ip_filter_options& opts) -> std::string
{
    if (opts.trusted_proxies.empty()) {
        return http::resolve_client_ip(ctx);
    }

    auto peer_ip = socket_peer_ip(ctx);
    if (ip_in_list(peer_ip, opts.trusted_proxies)) {
        return http::resolve_client_ip(ctx, peer_ip);
    }
    return peer_ip;
}

} // namespace detail

// =============================================================================
// ip_filter — IP filter middleware
// =============================================================================
//
// Behavior:
//   When whitelist is non-empty: IP in list → pass, otherwise → denied_status
//   When whitelist is empty + blacklist is non-empty: IP in list → denied_status, otherwise → pass
//   Both empty: pass through

export inline auto ip_filter(ip_filter_options opts = {}) -> http::middleware_fn
{
    return [opts = std::move(opts)]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        auto client_ip = detail::resolve_client_ip(ctx, opts);

        bool denied = false;

        if (!opts.allow_list.empty()) {
            // Whitelist mode: deny if not in list
            if (!detail::ip_in_list(client_ip, opts.allow_list))
                denied = true;
        } else if (!opts.deny_list.empty()) {
            // Blacklist mode: deny if in list
            if (detail::ip_in_list(client_ip, opts.deny_list))
                denied = true;
        }

        if (denied) {
            logger::warn("{} {} blocked IP: {}",
                         ctx.method(), ctx.path(), client_ip);
            ctx.json(opts.denied_status, std::format(
                R"({{"error":"access denied","ip":"{}"}})", client_ip));
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
