/**
 * @file ip_firewall.cppm
 * @brief IP adaptive firewall — frequency detection + auto-ban, based on
 * cache_store
 *
 * Automatically tracks IP violations (high-frequency requests, excessive
 * 4xx/5xx, path scanning, etc.), automatically adds IP to blacklist when
 * threshold exceeded, auto-unban after ban period expires. Uses cache_store
 * interface for state storage, seamlessly switchable between memory_cache /
 * redis_cache.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.ip_firewall;
 *   import
 * cnetmod.protocol.http.middleware.cache;
 * cnetmod::cache::memory_cache
 * store({.max_entries = 50000});
 *
 *   cnetmod::ip_firewall fw(store, {
 *       .max_violations     = 10,               // Max 10 violations in window
 *       .violation_window   = std::chrono::minutes{5},  // 5-minute tracking
 * window .ban_duration       = std::chrono::hours{1},    // Ban for 1 hour
 *       .track_4xx          = true,              // Count 4xx responses as
 * violations .track_5xx          = false,             // Don't count 5xx
 * (server issues) .track_rate_limit   = true,              // Count 429 as
 * violations
 *   });
 *
 *   // Ban check (place at front of middleware chain)
 *   svr.use(fw.check_middleware());
 *
 *   // Violation tracking (place at end of middleware chain, tracks after
 * handler execution) svr.use(fw.track_middleware());
 *
 *   // Manual violation report (e.g., from rate_limiter callback)
 *   co_await fw.report_violation(client_ip);
 *
 *   // Manual ban/unban
 *   co_await fw.ban(ip);
 *   co_await fw.unban(ip);
 *
 *   // Query status
 *   bool banned = co_await fw.is_banned(ip);
 *   int count = co_await fw.violation_count(ip);
 *
 *   // handler: expose ban status API (optional)
 *   router.get("/admin/firewall/:ip", cnetmod::firewall_status_handler(fw));
 *   router.post("/admin/firewall/:ip/ban", cnetmod::firewall_ban_handler(fw));
 *   router.del("/admin/firewall/:ip/ban", cnetmod::firewall_unban_handler(fw));
 */
export module cnetmod.protocol.http.middleware.ip_firewall;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.cache_store; // Only need abstract interface, avoid heavy dependencies like redis
// Note: Don't import cnetmod.core.log to avoid MSVC C1605 (object file exceeds
// 4GB) Use std::println(std::cerr, ...) for direct output

namespace cnetmod {

// =============================================================================
// ip_firewall_options — Firewall configuration
// =============================================================================

export struct ip_firewall_options {
  /// Violation threshold: ban when count reaches this value within window
  int max_violations = 10;

  /// Tracking window: TTL for violation counter
  std::chrono::seconds violation_window{300}; // 5 minutes

  /// Ban duration: auto-unban TTL
  std::chrono::seconds ban_duration{3600}; // 1 hour

  /// Whether to track 4xx responses (except 429)
  bool track_4xx = true;

  /// Whether to track 5xx responses
  bool track_5xx = false;

  /// Whether to track 429 (Too Many Requests) — works with rate_limiter
  bool track_rate_limit = true;

  /// Violation weights for specific paths (e.g., heavier penalty for scanning
  /// sensitive paths) key: path prefix, value: violation weight (default +1 per
  /// violation)
  std::vector<std::pair<std::string, int>> path_weights;

  /// Cache key prefix
  std::string key_prefix = "fw:";

  /// Response status code when banned
  int banned_status = http::status::forbidden;
};

// =============================================================================
// ip_firewall — Adaptive IP firewall
// =============================================================================

export class ip_firewall {
public:
  explicit ip_firewall(cache::cache_store &store,
                       ip_firewall_options opts = {}) noexcept;

  // =========================================================================
  // check_middleware — Ban check (place at head of middleware chain)
  // =========================================================================

  auto check_middleware() -> http::middleware_fn;

  // =========================================================================
  // track_middleware — Violation tracking (place at tail of middleware chain)
  // =========================================================================
  //
  // After handler execution, check response status code, accumulate violations
  // if conditions met. Auto-ban when threshold exceeded.

  auto track_middleware() -> http::middleware_fn;

  // =========================================================================
  // Programmatic API
  // =========================================================================

  /// Manual violation report (e.g., from rate_limiter callback)
  auto report_violation(std::string_view ip, int weight = 1) -> task<void>;

  /// Manual ban
  auto ban(std::string_view ip) -> task<void>;

  /// Manual ban (custom duration)
  auto ban(std::string_view ip, std::chrono::seconds duration) -> task<void>;

  /// Manual unban
  auto unban(std::string_view ip) -> task<void>;

  /// Query if IP is banned
  auto is_banned(std::string_view ip) -> task<bool>;

  /// Query current violation count for IP
  auto violation_count(std::string_view ip) -> task<int>;

private:
  cache::cache_store &store_;
  ip_firewall_options opts_;

  // =========================================================================
  // cache key generation
  // =========================================================================

  auto ban_key(const std::string &ip) const -> std::string;

  auto violation_key(const std::string &ip) const -> std::string;

  // =========================================================================
  // Violation accumulation + auto-ban decision
  // =========================================================================

  auto add_violation(const std::string &ip, int weight) -> task<void>;
};

} // namespace cnetmod

namespace cnetmod {

/// GET /admin/firewall/:ip — Query IP ban status
export auto firewall_status_handler(ip_firewall &fw) -> http::handler_fn;

/// POST /admin/firewall/:ip/ban — Manual ban
export auto firewall_ban_handler(ip_firewall &fw) -> http::handler_fn;

/// DELETE /admin/firewall/:ip/ban — Manual unban
export auto firewall_unban_handler(ip_firewall &fw) -> http::handler_fn;

} // namespace cnetmod
