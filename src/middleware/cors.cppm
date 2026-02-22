/**
 * @file cors.cppm
 * @brief CORS (Cross-Origin Resource Sharing) middleware
 *
 * Handles browser cross-origin requests: OPTIONS preflight auto-response + CORS headers added to all responses.
 *
 * Usage example:
 *   import cnetmod.middleware.cors;
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
export module cnetmod.middleware.cors;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// cors_options — CORS configuration
// =============================================================================

export struct cors_options {
    std::vector<std::string> allow_origins  = {"*"};
    std::vector<std::string> allow_methods  = {"GET","POST","PUT","DELETE","PATCH","OPTIONS"};
    std::vector<std::string> allow_headers  = {"Content-Type","Authorization","X-Request-ID"};
    std::vector<std::string> expose_headers = {"X-Request-ID"};
    bool allow_credentials = false;
    int  max_age = 86400;  // Preflight cache seconds (24h)
};

namespace detail {

inline auto join(const std::vector<std::string>& v, std::string_view sep)
    -> std::string
{
    std::string r;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) r += sep;
        r += v[i];
    }
    return r;
}

inline auto origin_allowed(const std::vector<std::string>& origins,
                           std::string_view origin) -> bool
{
    if (origins.empty()) return false;
    if (origins[0] == "*") return true;
    for (auto& o : origins)
        if (o == origin) return true;
    return false;
}

} // namespace detail

// =============================================================================
// cors — CORS middleware
// =============================================================================
//
// Behavior:
//   1. No Origin header → pass through (non-cross-origin request)
//   2. Origin not in allow list → pass through (browser will reject)
//   3. OPTIONS preflight → set CORS headers, return 204, don't call next()
//   4. Other requests → set CORS headers, call next()

export inline auto cors(cors_options opts = {}) -> http::middleware_fn
{
    return [opts = std::move(opts)]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        auto origin = ctx.get_header("Origin");
        if (origin.empty()) {
            co_await next();
            co_return;
        }

        if (!detail::origin_allowed(opts.allow_origins, origin)) {
            co_await next();
            co_return;
        }

        auto& resp = ctx.resp();

        // Set Access-Control-Allow-Origin
        if (opts.allow_origins.size() == 1
            && opts.allow_origins[0] == "*"
            && !opts.allow_credentials)
        {
            resp.set_header("Access-Control-Allow-Origin", "*");
        } else {
            resp.set_header("Access-Control-Allow-Origin", origin);
            resp.set_header("Vary", "Origin");
        }

        if (opts.allow_credentials)
            resp.set_header("Access-Control-Allow-Credentials", "true");

        if (!opts.expose_headers.empty())
            resp.set_header("Access-Control-Expose-Headers",
                            detail::join(opts.expose_headers, ", "));

        // OPTIONS preflight: short-circuit return 204
        if (ctx.method() == "OPTIONS") {
            resp.set_status(http::status::no_content);
            resp.set_header("Access-Control-Allow-Methods",
                            detail::join(opts.allow_methods, ", "));
            resp.set_header("Access-Control-Allow-Headers",
                            detail::join(opts.allow_headers, ", "));
            resp.set_header("Access-Control-Max-Age",
                            std::to_string(opts.max_age));
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
