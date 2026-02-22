/**
 * @file ip_firewall.cppm
 * @brief IP adaptive firewall — frequency detection + auto-ban, based on cache_store
 *
 * Automatically tracks IP violations (high-frequency requests, excessive 4xx/5xx, path scanning, etc.),
 * automatically adds IP to blacklist when threshold exceeded, auto-unban after ban period expires.
 * Uses cache_store interface for state storage, seamlessly switchable between memory_cache / redis_cache.
 *
 * Usage example:
 *   import cnetmod.middleware.ip_firewall;
 *   import cnetmod.middleware.cache;
 *
 *   cnetmod::cache::memory_cache store({.max_entries = 50000});
 *
 *   cnetmod::ip_firewall fw(store, {
 *       .max_violations     = 10,               // Max 10 violations in window
 *       .violation_window   = std::chrono::minutes{5},  // 5-minute tracking window
 *       .ban_duration       = std::chrono::hours{1},    // Ban for 1 hour
 *       .track_4xx          = true,              // Count 4xx responses as violations
 *       .track_5xx          = false,             // Don't count 5xx (server issues)
 *       .track_rate_limit   = true,              // Count 429 as violations
 *   });
 *
 *   // Ban check (place at front of middleware chain)
 *   svr.use(fw.check_middleware());
 *
 *   // Violation tracking (place at end of middleware chain, tracks after handler execution)
 *   svr.use(fw.track_middleware());
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
export module cnetmod.middleware.ip_firewall;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.middleware.cache_store;  // Only need abstract interface, avoid heavy dependencies like redis
// Note: Don't import cnetmod.core.log to avoid MSVC C1605 (object file exceeds 4GB)
// Use std::println(std::cerr, ...) for direct output

namespace cnetmod {

// =============================================================================
// ip_firewall_options — Firewall configuration
// =============================================================================

export struct ip_firewall_options {
    /// Violation threshold: ban when count reaches this value within window
    int max_violations = 10;

    /// Tracking window: TTL for violation counter
    std::chrono::seconds violation_window{300};  // 5 minutes

    /// Ban duration: auto-unban TTL
    std::chrono::seconds ban_duration{3600};     // 1 hour

    /// Whether to track 4xx responses (except 429)
    bool track_4xx = true;

    /// Whether to track 5xx responses
    bool track_5xx = false;

    /// Whether to track 429 (Too Many Requests) — works with rate_limiter
    bool track_rate_limit = true;

    /// Violation weights for specific paths (e.g., heavier penalty for scanning sensitive paths)
    /// key: path prefix, value: violation weight (default +1 per violation)
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
    explicit ip_firewall(cache::cache_store& store,
                         ip_firewall_options opts = {}) noexcept
        : store_(store), opts_(std::move(opts)) {}

    // =========================================================================
    // check_middleware — Ban check (place at head of middleware chain)
    // =========================================================================

    auto check_middleware() -> http::middleware_fn {
        return [this](http::request_context& ctx,
                      http::next_fn next) -> task<void>
        {
            auto ip = http::resolve_client_ip(ctx);
            auto banned = co_await is_banned(ip);
            if (banned) {
                // Get remaining ban time (best effort estimate)
                std::println(std::cerr, "[firewall] blocked banned IP: {}", ip);
                ctx.resp().set_header("Connection", "close");
                ctx.json(opts_.banned_status, std::format(
                    R"({{"error":"ip banned","ip":"{}"}})", ip));
                co_return;
            }
            co_await next();
        };
    }

    // =========================================================================
    // track_middleware — Violation tracking (place at tail of middleware chain)
    // =========================================================================
    //
    // After handler execution, check response status code, accumulate violations if conditions met.
    // Auto-ban when threshold exceeded.

    auto track_middleware() -> http::middleware_fn {
        return [this](http::request_context& ctx,
                      http::next_fn next) -> task<void>
        {
            co_await next();

            auto status = ctx.resp().status_code();
            int weight = 0;

            // 429 detection
            if (opts_.track_rate_limit && status == 429) {
                weight = 1;
            }
            // 4xx detection (exclude 429, already handled separately)
            else if (opts_.track_4xx && status >= 400 && status < 500
                     && status != 429)
            {
                weight = 1;
            }
            // 5xx detection
            else if (opts_.track_5xx && status >= 500) {
                weight = 1;
            }

            if (weight <= 0) co_return;

            // Check path weights
            auto path = ctx.path();
            for (auto& [prefix, w] : opts_.path_weights) {
                if (path.starts_with(prefix)) {
                    weight = w;
                    break;
                }
            }

            auto ip = http::resolve_client_ip(ctx);
            co_await add_violation(ip, weight);
        };
    }

    // =========================================================================
    // Programmatic API
    // =========================================================================

    /// Manual violation report (e.g., from rate_limiter callback)
    auto report_violation(std::string_view ip, int weight = 1) -> task<void> {
        co_await add_violation(std::string(ip), weight);
    }

    /// Manual ban
    auto ban(std::string_view ip) -> task<void> {
        co_await ban(std::string(ip), opts_.ban_duration);
    }

    /// Manual ban (custom duration)
    auto ban(std::string_view ip, std::chrono::seconds duration) -> task<void> {
        auto key = ban_key(std::string(ip));
        co_await store_.set(key, "1", duration);
        std::println(std::cerr, "[firewall] banned IP: {} for {}s", ip, duration.count());
    }

    /// Manual unban
    auto unban(std::string_view ip) -> task<void> {
        auto key = ban_key(std::string(ip));
        co_await store_.del(key);
        // Also clear violation count
        auto vk = violation_key(std::string(ip));
        co_await store_.del(vk);
        std::println(std::cerr, "[firewall] unbanned IP: {}", ip);
    }

    /// Query if IP is banned
    auto is_banned(std::string_view ip) -> task<bool> {
        auto key = ban_key(std::string(ip));
        co_return co_await store_.exists(key);
    }

    /// Query current violation count for IP
    auto violation_count(std::string_view ip) -> task<int> {
        auto key = violation_key(std::string(ip));
        auto val = co_await store_.get(key);
        if (!val) co_return 0;
        int count = 0;
        auto [ptr, ec] = std::from_chars(val->data(),
                                          val->data() + val->size(), count);
        co_return (ec == std::errc{}) ? count : 0;
    }

private:
    cache::cache_store& store_;
    ip_firewall_options opts_;

    // =========================================================================
    // cache key generation
    // =========================================================================

    auto ban_key(const std::string& ip) const -> std::string {
        return opts_.key_prefix + "ban:" + ip;
    }

    auto violation_key(const std::string& ip) const -> std::string {
        return opts_.key_prefix + "v:" + ip;
    }

    // =========================================================================
    // Violation accumulation + auto-ban decision
    // =========================================================================

    auto add_violation(const std::string& ip, int weight) -> task<void> {
        // No need to continue accumulating for already banned IPs
        if (co_await is_banned(ip)) co_return;

        auto key = violation_key(ip);

        // Read current count
        int count = 0;
        auto val = co_await store_.get(key);
        if (val) {
            auto [ptr, ec] = std::from_chars(val->data(),
                                              val->data() + val->size(), count);
            if (ec != std::errc{}) count = 0;
        }

        count += weight;

        // Write back (refresh TTL = violation_window)
        co_await store_.set(key, std::to_string(count), opts_.violation_window);

        // debug level logging, enable only when needed
        // std::println(std::cerr, "[firewall] violation: ip={} count={}/{}", ip, count,
        //              opts_.max_violations);

        // Exceeded threshold → ban
        if (count >= opts_.max_violations) {
            co_await ban(ip, opts_.ban_duration);
            // Clear violation count (restart after ban)
            co_await store_.del(key);
        }
    }

};

} // namespace cnetmod

namespace cnetmod {

/// GET /admin/firewall/:ip — Query IP ban status
export inline auto firewall_status_handler(ip_firewall& fw) -> http::handler_fn {
    return [&fw](http::request_context& ctx) -> task<void> {
        auto ip = ctx.param("ip");
        if (ip.empty()) {
            ctx.json(http::status::bad_request,
                     R"({"error":"missing ip param"})");
            co_return;
        }
        auto banned = co_await fw.is_banned(ip);
        auto count  = co_await fw.violation_count(ip);
        ctx.json(http::status::ok, std::format(
            R"({{"ip":"{}","banned":{},"violations":{}}})",
            ip, banned ? "true" : "false", count));
    };
}

/// POST /admin/firewall/:ip/ban — Manual ban
export inline auto firewall_ban_handler(ip_firewall& fw) -> http::handler_fn {
    return [&fw](http::request_context& ctx) -> task<void> {
        auto ip = ctx.param("ip");
        if (ip.empty()) {
            ctx.json(http::status::bad_request,
                     R"({"error":"missing ip param"})");
            co_return;
        }
        co_await fw.ban(ip);
        ctx.json(http::status::ok, std::format(
            R"({{"ip":"{}","banned":true}})", ip));
    };
}

/// DELETE /admin/firewall/:ip/ban — Manual unban
export inline auto firewall_unban_handler(ip_firewall& fw) -> http::handler_fn {
    return [&fw](http::request_context& ctx) -> task<void> {
        auto ip = ctx.param("ip");
        if (ip.empty()) {
            ctx.json(http::status::bad_request,
                     R"({"error":"missing ip param"})");
            co_return;
        }
        co_await fw.unban(ip);
        ctx.json(http::status::ok, std::format(
            R"({{"ip":"{}","banned":false}})", ip));
    };
}

} // namespace cnetmod
