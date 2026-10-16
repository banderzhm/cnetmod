export module cnetmod.protocol.http:utils;
import std;
import :router;
import cnetmod.utils.flat_map;

export namespace cnetmod::http {
/**
 * @brief Reports whether an IPv4/IPv6 address equals or falls inside a CIDR.
 *
 * Bracketed IPv6 literals are accepted on both sides.
 */
[[nodiscard]] auto ip_matches(std::string_view address, std::string_view pattern) -> bool;

/**
 * @brief Resolves the client address behind a chain of trusted proxies.
 *
 * Forwarding headers are client-controlled. They are honored only when the
 * TCP peer is a trusted proxy; X-Forwarded-For is then walked right to left
 * and the first hop that is not a trusted proxy is the client. Without a
 * trusted peer the peer address itself is the client. Pure function so the
 * policy can be tested without sockets.
 *
 * @return the client address, or "unknown" when no address is available
 */
[[nodiscard]] auto resolve_forwarded_client_ip(std::string_view peer_address,
    std::string_view x_forwarded_for, std::string_view x_real_ip,
    std::span<const std::string> trusted_proxies) -> std::string;

/**
 * @brief Client address of a request; see resolve_forwarded_client_ip().
 *
 * With no trusted proxies configured forwarding headers are ignored, so a
 * client cannot choose its own rate-limit, firewall or allow-list identity.
 */
[[nodiscard]] auto resolve_client_ip(const request_context& request,
    std::span<const std::string> trusted_proxies = {}) -> std::string;

[[nodiscard]] auto parse_query_param(std::string_view query, std::string_view key) -> std::string;

[[nodiscard]] auto parse_query_params(std::string_view query)
    -> cnetmod::flat_map<std::string, std::string>;
} // namespace cnetmod::http

/**/
