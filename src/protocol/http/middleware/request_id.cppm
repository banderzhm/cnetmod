/**
 * @file request_id.cppm
 * @brief Request Tracking ID Middleware — Generate/Propagate X-Request-ID
 *
 * Assigns unique ID to each request, writes to response header. If request
 * already carries X-Request-ID (reverse proxy chain), reuses that ID for
 * distributed tracing.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.request_id;
 *
 *   svr.use(request_id());
 *   // Handler can get current request ID via
 * ctx.resp().get_header("X-Request-ID")
 */
export module cnetmod.protocol.http.middleware.request_id;

import std;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// request_id — Request Tracking ID Middleware
// =============================================================================
//
// Behavior:
//   1. Request already has X-Request-ID → Reuse (passed from reverse
//   proxy/gateway)
//   2. No ID → Generate 128-bit random hex
//   3. Write to response header X-Request-ID
//   4. Handler reads via ctx.resp().get_header("X-Request-ID")

export auto request_id(std::string_view header_name = "X-Request-ID")
    -> http::middleware_fn;

} // namespace cnetmod
