/**
 * @brief Shared Bearer-token parsing for synchronous and coroutine authentication.
 *
 * Application-specific verification and principal binding live in callbacks;
 * this middleware owns header parsing and the rejection flow. Whether a
 * request needs credentials is declared on the matched route through endpoint
 * metadata:
 *
 * - http::allow_anonymous: credentials are not parsed at all;
 * - http::optional_authentication: a valid credential binds the principal and
 *   an absent credential continues anonymously;
 * - no authentication metadata: credentials are required.
 *
 * Requests that match no route use jwt_auth_options::unmatched, which defaults
 * to skip so the router answers 404 without disclosing authentication rules.
 */
export module cnetmod.protocol.http.middleware.jwt_auth;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

export struct jwt_auth_failure
{
    int status = http::status::unauthorized;
    std::string message = "invalid or expired token";
};

export enum class jwt_auth_mode
{
    required,
    optional,
    skip,
};

/**
 * @brief Behavior when an optional route receives a credential that fails.
 */
export enum class invalid_optional_credentials
{
    /// A presented but malformed, expired or revoked credential is rejected
    /// with 401 so a client never silently loses its identity.
    reject,
    /// The request continues anonymously as if no credential was presented.
    continue_anonymous,
};

export struct jwt_auth_options
{
    /// Token verification function: returns true if valid
    std::function<bool(std::string_view token)> verify;

    /// Optional per-request override. When empty the mode is derived from the
    /// matched endpoint metadata (see endpoint_authentication_mode()).
    std::function<jwt_auth_mode(const http::request_context&)> mode_for;

    /// Mode for requests that matched no route.
    jwt_auth_mode unmatched = jwt_auth_mode::skip;

    /// Handling of presented-but-invalid credentials on optional routes.
    /// Authenticator failures other than 401 (notably infrastructure errors)
    /// are always rejected.
    invalid_optional_credentials invalid_optional =
        invalid_optional_credentials::reject;

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

/**
 * @brief Resolves the authentication mode declared on the matched endpoint.
 */
export [[nodiscard]] inline auto endpoint_authentication_mode(
    const http::request_context& ctx, jwt_auth_mode unmatched) noexcept
    -> jwt_auth_mode
{
    const auto* endpoint = ctx.endpoint();
    if (endpoint == nullptr)
        return unmatched;
    if (endpoint->metadata.contains<http::allow_anonymous>())
        return jwt_auth_mode::skip;
    if (endpoint->metadata.contains<http::optional_authentication>())
        return jwt_auth_mode::optional;
    return jwt_auth_mode::required;
}

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

        const auto mode = opts.mode_for
            ? opts.mode_for(ctx)
            : endpoint_authentication_mode(ctx, opts.unmatched);
        if (mode == jwt_auth_mode::skip)
        {
            co_await next();
            co_return;
        }

        // An absent credential is the only case an optional route always
        // accepts anonymously; a presented credential must be valid unless the
        // application explicitly opts into anonymous fallback.
        const bool anonymous_on_invalid = mode == jwt_auth_mode::optional &&
            opts.invalid_optional ==
                invalid_optional_credentials::continue_anonymous;

        const auto authorization = ctx.get_header(opts.header_name);
        if (authorization.empty())
        {
            if (mode == jwt_auth_mode::optional)
            {
                co_await next();
                co_return;
            }
            reject({.message = "missing authorization header"},
                R"({"error":"missing authorization header"})");
            co_return;
        }

        if (!opts.token_prefix.empty() &&
            !authorization.starts_with(opts.token_prefix))
        {
            if (anonymous_on_invalid)
            {
                co_await next();
                co_return;
            }
            reject({.message = "invalid authorization format"},
                R"({"error":"invalid authorization format"})");
            co_return;
        }

        const auto token = std::string_view{authorization}.substr(
            opts.token_prefix.size());
        if (token.empty())
        {
            if (anonymous_on_invalid)
            {
                co_await next();
                co_return;
            }
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
                if (anonymous_on_invalid &&
                    result.error().status == http::status::unauthorized)
                {
                    co_await next();
                    co_return;
                }
                reject(std::move(result.error()),
                    R"({"error":"invalid or expired token"})");
                co_return;
            }
        }
        else if (!opts.verify || !opts.verify(token))
        {
            if (anonymous_on_invalid)
            {
                co_await next();
                co_return;
            }
            reject({.message = "invalid or expired token"},
                R"({"error":"invalid or expired token"})");
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
