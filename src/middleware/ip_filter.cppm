/**
 * @file ip_filter.cppm
 * @brief IP whitelist/blacklist middleware — access control based on client IP
 *
 * Supports whitelist mode (only allow IPs in list) and blacklist mode (deny IPs in list).
 * Automatically parses X-Forwarded-For / X-Real-IP to get real client IP.
 *
 * Usage example:
 *   import cnetmod.middleware.ip_filter;
 *
 *   // Whitelist: only allow internal network
 *   svr.use(ip_filter({.allow_list = {"127.0.0.1", "10.0.0.0/8"}}));
 *
 *   // Blacklist: ban specific IPs
 *   svr.use(ip_filter({.deny_list = {"1.2.3.4", "5.6.7.8"}}));
 */
export module cnetmod.middleware.ip_filter;

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

    /// HTTP status code when denied
    int denied_status = http::status::forbidden;
};

// =============================================================================
// Internal: IP parsing and matching
// =============================================================================

namespace detail {

/// Simple CIDR matching (IPv4 only)
/// Format: "10.0.0.0/8" or exact IP "192.168.1.1"
inline auto ip_matches(std::string_view client_ip,
                       std::string_view pattern) -> bool
{
    // Exact match
    if (client_ip == pattern)
        return true;

    // CIDR match
    auto slash = pattern.find('/');
    if (slash == std::string_view::npos)
        return false;

    auto net_str = pattern.substr(0, slash);
    auto bits_str = pattern.substr(slash + 1);
    int bits = 0;
    std::from_chars(bits_str.data(), bits_str.data() + bits_str.size(), bits);
    if (bits < 0 || bits > 32) return false;

    // Parse IPv4 to uint32
    auto parse_ipv4 = [](std::string_view s) -> std::optional<std::uint32_t> {
        std::uint32_t result = 0;
        int octet_count = 0;
        std::size_t pos = 0;
        while (pos <= s.size() && octet_count < 4) {
            auto dot = s.find('.', pos);
            if (dot == std::string_view::npos) dot = s.size();
            int val = 0;
            auto [ptr, ec] = std::from_chars(
                s.data() + pos, s.data() + dot, val);
            if (ec != std::errc{} || val < 0 || val > 255) return std::nullopt;
            result = (result << 8) | static_cast<std::uint32_t>(val);
            ++octet_count;
            pos = dot + 1;
        }
        return (octet_count == 4) ? std::optional{result} : std::nullopt;
    };

    auto client = parse_ipv4(client_ip);
    auto network = parse_ipv4(net_str);
    if (!client || !network) return false;

    if (bits == 0) return true;
    std::uint32_t mask = ~std::uint32_t{0} << (32 - bits);
    return (*client & mask) == (*network & mask);
}

/// Check if IP is in list (supports CIDR)
inline auto ip_in_list(std::string_view ip,
                       const std::vector<std::string>& list) -> bool
{
    for (auto& pattern : list)
        if (ip_matches(ip, pattern)) return true;
    return false;
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
        auto client_ip = http::resolve_client_ip(ctx);

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
