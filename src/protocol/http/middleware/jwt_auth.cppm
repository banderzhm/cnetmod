/**
 * @file jwt_auth.cppm
 * @brief JWT / Bearer Token authentication middleware
 *
 * Extracts Bearer token from Authorization header and calls user-provided verification function.
 * Pluggable verification logic supports cnetmod JWT, custom HMAC, API keys,
 * and application-defined schemes.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.jwt_auth;
 *
 *   // Simple API Key verification
 *   svr.use(jwt_auth({
 *       .verify = [](std::string_view token) { return token == "my-secret"; },
 *       .skip_paths = {"/", "/login"},
 *   }));
 *
 *   // Verify HS256 with cnetmod.security.jwt in the application layer.
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
export module cnetmod.protocol.http.middleware.jwt_auth;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// generate_secure_token — CSPRNG secure token (hex encoded)
// =============================================================================

/// Generate cryptographically secure random token (hex encoded)
/// MSVC uses BCryptGenRandom, GCC/Clang uses /dev/urandom
export inline auto generate_secure_token(std::size_t bytes = 32) -> std::string
{
    static thread_local std::random_device rd;
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i)
    {
        auto byte = static_cast<std::uint8_t>(rd() & 0xFF);
        token.push_back(hex[(byte >> 4) & 0x0F]);
        token.push_back(hex[byte & 0x0F]);
    }
    return token;
}

// =============================================================================
// jwt_auth_options — JWT authentication configuration
// =============================================================================

export struct jwt_auth_failure
{
    int status = http::status::unauthorized;
    std::string message = "invalid or expired token";
};

export struct jwt_auth_options
{
    /// Token verification function: returns true if valid
    std::function<bool(std::string_view token)> verify;

    /// Paths to skip authentication (exact match or prefix match path + "/")
    std::vector<std::string> skip_paths;

    /// Request header to read token from (default: Authorization)
    std::string header_name = "Authorization";

    /// Token prefix (default: "Bearer "), set to empty to use entire header value
    std::string token_prefix = "Bearer ";

    /// Coroutine authentication can verify a token and bind a request principal.
    /// When set, this takes precedence over the synchronous verify callback.
    std::function<task<std::expected<void, jwt_auth_failure>>(
        http::request_context&, std::string_view)> authenticate_async;

    /// Optional application-specific response envelope for authentication failures.
    std::function<void(http::request_context&,
        const jwt_auth_failure&)> on_failure;
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
    return [opts = std::move(opts)](http::request_context& ctx, http::next_fn next) -> task<void>
    {
        auto reject = [&opts, &ctx](jwt_auth_failure failure,
            std::string_view default_body)
        {
            if (opts.on_failure)
                opts.on_failure(ctx, failure);
            else
                ctx.json(failure.status, default_body);
        };

        // Skip specified paths
        auto path = ctx.path();
        for (auto& skip : opts.skip_paths)
        {
            if (path == skip || (!skip.empty() && skip != "/" && path.starts_with(skip) && (path.size() == skip.size() || path[skip.size()] == '/')))
            {
                co_await next();
                co_return;
            }
        }

        // Extract token
        auto auth = ctx.get_header(opts.header_name);
        if (auth.empty())
        {
            reject({http::status::unauthorized,
                "missing authorization header"},
                R"({"error":"missing authorization header"})");
            co_return;
        }

        std::string_view token = auth;
        if (!opts.token_prefix.empty())
        {
            if (!auth.starts_with(opts.token_prefix))
            {
                reject({http::status::unauthorized,
                    "invalid authorization format"},
                    R"({"error":"invalid authorization format"})");
                co_return;
            }
            token = auth.substr(opts.token_prefix.size());
        }

        if (token.empty())
        {
            reject({http::status::unauthorized,
                "invalid authorization format"},
                R"({"error":"invalid authorization format"})");
            co_return;
        }

        // Own the token across suspension in a coroutine authenticator.
        const std::string owned_token{token};
        if (opts.authenticate_async)
        {
            auto result = co_await opts.authenticate_async(ctx, owned_token);
            if (!result)
            {
                reject(std::move(result.error()),
                    R"({"error":"invalid or expired token"})");
                co_return;
            }
        }
        else if (!opts.verify || !opts.verify(owned_token))
        {
            reject({http::status::unauthorized,
                "invalid or expired token"},
                R"({"error":"invalid or expired token"})");
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
