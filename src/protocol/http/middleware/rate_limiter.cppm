export module cnetmod.protocol.http.middleware.rate_limiter;

import std;
import cnetmod.protocol.http;

export namespace cnetmod {
struct rate_limiter_options
{
    double rate = 10.0;
    double burst = 20.0;
    /// Bucket key. Defaults to the client address resolved through
    /// trusted_proxies; forwarding headers from other peers are ignored.
    std::function<std::string(http::request_context&)> key_fn;
    std::chrono::seconds entry_ttl{300};
    /// Proxies (addresses or CIDRs) whose forwarding headers are honored by
    /// the default key.
    std::vector<std::string> trusted_proxies;
    /// Writes the rejection in the application's response envelope. The
    /// middleware has already set Retry-After; the default writes a small
    /// JSON body with status 429.
    std::function<void(http::request_context&, std::chrono::seconds retry_after)>
        on_limited;
};

/**
 * Create one process-local limiter state shared by every copy of the returned
 * middleware. Installing that middleware once on a multi-event-loop server
 * therefore enforces one global budget per key, not one budget per loop.
 * Separate calls to rate_limiter() intentionally create independent budgets.
 */
[[nodiscard]] auto rate_limiter(rate_limiter_options opts = {})
    -> http::middleware_fn;
} // namespace cnetmod
