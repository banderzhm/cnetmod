/**
 * @file request_id.cppm
 * @brief Request Tracking ID Middleware — Generate/Propagate X-Request-ID
 *
 * Assigns unique ID to each request, writes to response header. If request already
 * carries X-Request-ID (reverse proxy chain), reuses that ID for distributed tracing.
 *
 * Usage example:
 *   import cnetmod.middleware.request_id;
 *
 *   svr.use(request_id());
 *   // Handler can get current request ID via ctx.resp().get_header("X-Request-ID")
 */
export module cnetmod.middleware.request_id;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

namespace detail {

/// Generate 128-bit random hex ID (CSPRNG)
inline auto generate_request_id() -> std::string {
    static thread_local std::random_device rd;
    static constexpr char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 16; ++i) {
        auto byte = static_cast<std::uint8_t>(rd() & 0xFF);
        id.push_back(hex[(byte >> 4) & 0x0F]);
        id.push_back(hex[byte & 0x0F]);
    }
    return id;
}

} // namespace detail

// =============================================================================
// request_id — Request Tracking ID Middleware
// =============================================================================
//
// Behavior:
//   1. Request already has X-Request-ID → Reuse (passed from reverse proxy/gateway)
//   2. No ID → Generate 128-bit random hex
//   3. Write to response header X-Request-ID
//   4. Handler reads via ctx.resp().get_header("X-Request-ID")

export inline auto request_id(std::string_view header_name = "X-Request-ID")
    -> http::middleware_fn
{
    return [hdr = std::string(header_name)]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        auto existing = ctx.get_header(hdr);
        auto id = existing.empty()
                  ? detail::generate_request_id()
                  : std::string(existing);

        ctx.resp().set_header(hdr, id);

        co_await next();
    };
}

} // namespace cnetmod
