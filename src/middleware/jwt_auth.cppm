/**
 * @file jwt_auth.cppm
 * @brief JWT / Bearer Token authentication middleware
 *
 * Extracts Bearer token from Authorization header and calls user-provided verification function.
 * Pluggable verification logic: supports jwt-cpp, custom HMAC, API Key, and any other schemes.
 *
 * Usage example:
 *   import cnetmod.middleware.jwt_auth;
 *
 *   // Simple API Key verification
 *   svr.use(jwt_auth({
 *       .verify = [](std::string_view token) { return token == "my-secret"; },
 *       .skip_paths = {"/", "/login"},
 *   }));
 *
 *   // Using jwt-cpp to verify HS256 (in application layer #include <jwt-cpp/jwt.h>)
 *   svr.use(jwt_auth({
 *       .verify = [](std::string_view token) {
 *           try {
 *               auto decoded = jwt::decode(std::string(token));
 *               auto verifier = jwt::verify()
 *                   .allow_algorithm(jwt::algorithm::hs256{"secret"})
 *                   .with_issuer("myapp");
 *               verifier.verify(decoded);
 *               return true;
 *           } catch (...) { return false; }
 *       },
 *       .skip_paths = {"/", "/login", "/register"},
 *   }));
 */
export module cnetmod.middleware.jwt_auth;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// generate_secure_token — CSPRNG secure token (hex encoded)
// =============================================================================

/// Generate cryptographically secure random token (hex encoded)
/// MSVC uses BCryptGenRandom, GCC/Clang uses /dev/urandom
export inline auto generate_secure_token(std::size_t bytes = 32) -> std::string {
    static thread_local std::random_device rd;
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        auto byte = static_cast<std::uint8_t>(rd() & 0xFF);
        token.push_back(hex[(byte >> 4) & 0x0F]);
        token.push_back(hex[byte & 0x0F]);
    }
    return token;
}

// =============================================================================
// jwt_auth_options — JWT authentication configuration
// =============================================================================

export struct jwt_auth_options {
    /// Token verification function: returns true if valid
    std::function<bool(std::string_view token)> verify;

    /// Paths to skip authentication (exact match or prefix match path + "/")
    std::vector<std::string> skip_paths;

    /// Request header to read token from (default: Authorization)
    std::string header_name = "Authorization";

    /// Token prefix (default: "Bearer "), set to empty to use entire header value
    std::string token_prefix = "Bearer ";
};

// =============================================================================
// jwt_auth — Bearer Token authentication middleware
// =============================================================================
//
// Flow:
//   1. Check skip_paths → if matched, pass through
//   2. Read Authorization header → if empty, return 401
//   3. Remove "Bearer " prefix → if format invalid, return 401
//   4. Call verify(token) → if false, return 401
//   5. Pass → call next()

export inline auto jwt_auth(jwt_auth_options opts) -> http::middleware_fn
{
    return [opts = std::move(opts)]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        // Skip specified paths
        auto path = ctx.path();
        for (auto& skip : opts.skip_paths) {
            if (path == skip
                || (!skip.empty() && skip != "/"
                    && path.starts_with(skip)
                    && (path.size() == skip.size()
                        || path[skip.size()] == '/')))
            {
                co_await next();
                co_return;
            }
        }

        // Extract token
        auto auth = ctx.get_header(opts.header_name);
        if (auth.empty()) {
            ctx.json(http::status::unauthorized,
                R"({"error":"missing authorization header"})");
            co_return;
        }

        std::string_view token = auth;
        if (!opts.token_prefix.empty()) {
            if (!auth.starts_with(opts.token_prefix)) {
                ctx.json(http::status::unauthorized,
                    R"({"error":"invalid authorization format"})");
                co_return;
            }
            token = auth.substr(opts.token_prefix.size());
        }

        // Verify
        if (!opts.verify || !opts.verify(token)) {
            ctx.json(http::status::unauthorized,
                R"({"error":"invalid or expired token"})");
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
