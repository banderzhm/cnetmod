/**
 * @brief Shared Bearer-token parsing for synchronous and coroutine authentication.
 *
 * Application-specific verification and principal binding live in callbacks;
 * this middleware owns header parsing, public paths, and rejection flow.
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
        http::request_context&, std::string_view)>
        authenticate_async;

    /// Optional application-specific response envelope for authentication failures.
    std::function<void(http::request_context&,
        const jwt_auth_failure&)>
        on_failure;
};

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

        const auto path = ctx.path();
        const auto is_public = std::ranges::any_of(opts.skip_paths,
            [path](const std::string& prefix)
            {
                return path == prefix ||
                    (!prefix.empty() && prefix != "/" &&
                        path.starts_with(prefix) &&
                        path.size() > prefix.size() &&
                        path[prefix.size()] == '/');
            });
        if (is_public)
        {
            co_await next();
            co_return;
        }

        const auto authorization = ctx.get_header(opts.header_name);
        if (authorization.empty())
        {
            reject({.message = "missing authorization header"},
                R"({"error":"missing authorization header"})");
            co_return;
        }

        if (!opts.token_prefix.empty() &&
            !authorization.starts_with(opts.token_prefix))
        {
            reject({.message = "invalid authorization format"},
                R"({"error":"invalid authorization format"})");
            co_return;
        }

        const auto token = std::string_view{authorization}.substr(
            opts.token_prefix.size());
        if (token.empty())
        {
            reject({.message = "invalid authorization format"},
                R"({"error":"invalid authorization format"})");
            co_return;
        }

        if (opts.authenticate_async)
        {
            // The request may suspend while verifying; retain the token value.
            const std::string owned_token{token};
            auto result = co_await opts.authenticate_async(ctx, owned_token);
            if (!result)
            {
                reject(std::move(result.error()),
                    R"({"error":"invalid or expired token"})");
                co_return;
            }
        }
        else if (!opts.verify || !opts.verify(token))
        {
            reject({.message = "invalid or expired token"},
                R"({"error":"invalid or expired token"})");
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
