/**
 * @file cors.cppm
 * @brief CORS (Cross-Origin Resource Sharing) middleware
 *
 * Handles browser cross-origin requests: OPTIONS preflight auto-response + CORS
 * headers added to all responses.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.cors;
 *
 *   // Default: allow all Origins
 *   svr.use(cors());
 *
 *   // Custom
 *   svr.use(cors({
 *       .allow_origins = {"https://example.com", "https://app.example.com"},
 *       .allow_credentials = true,
 *       .max_age = 3600,
 *   }));
 */
export module cnetmod.protocol.http.middleware.cors;

import std;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// cors_options — CORS configuration
// =============================================================================

export struct cors_options {
  std::vector<std::string> allow_origins = {"*"};
  std::vector<std::string> allow_methods = {"GET",    "POST",  "PUT",
                                            "DELETE", "PATCH", "OPTIONS"};
  std::vector<std::string> allow_headers = {"Content-Type", "Authorization",
                                            "X-Request-ID"};
  std::vector<std::string> expose_headers = {"X-Request-ID"};
  bool allow_credentials = false;
  int max_age = 86400; // Preflight cache seconds (24h)
};

// =============================================================================
// cors — CORS middleware
// =============================================================================
//
// Behavior:
//   1. No Origin header → pass through (non-cross-origin request)
//   2. Origin not in allow list → pass through (browser will reject)
//   3. OPTIONS preflight → set CORS headers, return 204, don't call next()
//   4. Other requests → set CORS headers, call next()

export auto cors(cors_options opts = {}) -> http::middleware_fn;

} // namespace cnetmod
