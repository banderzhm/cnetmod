/**
 * @file utils.cppm
 * @brief HTTP Common Utility Functions
 *
 * Provides HTTP helper functions shared across multiple middleware and handlers,
 * avoiding duplicate implementations of the same logic.
 *
 * Function List:
 *   resolve_client_ip  — Parse real client IP from reverse proxy headers
 *   parse_query_param  — Extract single parameter value from query string
 *   parse_query_params — Parse complete query string into key-value map
 *
 * Usage Example:
 *   import cnetmod.protocol.http;
 *
 *   // Parse real IP (supports X-Forwarded-For / X-Real-IP)
 *   auto ip = http::resolve_client_ip(ctx);
 *
 *   // Extract query parameter
 *   auto type = http::parse_query_param(ctx.query_string(), "type");
 *
 *   // Parse all parameters
 *   auto params = http::parse_query_params(ctx.query_string());
 */
export module cnetmod.protocol.http:utils;

import std;
import :router;   // request_context

namespace cnetmod::http {

// =============================================================================
// resolve_client_ip — Parse Real Client IP
// =============================================================================
//
// Priority (high to low):
//   1. X-Forwarded-For  leftmost IP (original client), trim whitespace
//   2. X-Real-IP
//   3. fallback (default "unknown")
//
// Use Case: Get real source IP when behind reverse proxy like Nginx / Cloudflare.

export inline auto resolve_client_ip(
    const request_context& ctx,
    std::string_view fallback = "unknown") -> std::string
{
    // X-Forwarded-For: client, proxy1, proxy2
    if (auto xff = ctx.get_header("X-Forwarded-For"); !xff.empty()) {
        auto comma = xff.find(',');
        auto ip = (comma != std::string_view::npos) ? xff.substr(0, comma) : xff;
        while (!ip.empty() && ip.front() == ' ') ip.remove_prefix(1);
        while (!ip.empty() && ip.back()  == ' ') ip.remove_suffix(1);
        if (!ip.empty()) return std::string(ip);
    }
    if (auto xri = ctx.get_header("X-Real-IP"); !xri.empty())
        return std::string(xri);
    return std::string(fallback);
}

// =============================================================================
// parse_query_param — Extract Single Parameter Value from Query String
// =============================================================================
//
// Correctly handles boundaries: avoids "mykey=v" being matched by "key=".
//
// Example:
//   parse_query_param("type=send_email&params=to%40x.com", "params")
//   → "to%40x.com"
//
// Note: Does not perform URL decoding, caller handles %xx escaping as needed.

export inline auto parse_query_param(
    std::string_view query,
    std::string_view key) -> std::string
{
    std::size_t pos = 0;
    while (pos < query.size()) {
        auto amp = query.find('&', pos);
        auto seg = (amp != std::string_view::npos)
                   ? query.substr(pos, amp - pos)
                   : query.substr(pos);

        auto eq = seg.find('=');
        if (eq != std::string_view::npos && seg.substr(0, eq) == key)
            return std::string(seg.substr(eq + 1));

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return {};
}

// =============================================================================
// parse_query_params — Parse Complete Query String into Key-Value Map
// =============================================================================
//
// Example:
//   parse_query_params("a=1&b=hello&c=")
//   → {{"a","1"}, {"b","hello"}, {"c",""}}
//
// Note: For duplicate keys, later value overwrites earlier. Does not perform URL decoding.

export inline auto parse_query_params(std::string_view query)
    -> std::unordered_map<std::string, std::string>
{
    std::unordered_map<std::string, std::string> result;
    std::size_t pos = 0;
    while (pos < query.size()) {
        auto amp = query.find('&', pos);
        auto seg = (amp != std::string_view::npos)
                   ? query.substr(pos, amp - pos)
                   : query.substr(pos);

        auto eq = seg.find('=');
        if (eq != std::string_view::npos)
            result.insert_or_assign(std::string(seg.substr(0, eq)),
                                    std::string(seg.substr(eq + 1)));

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return result;
}

} // namespace cnetmod::http
