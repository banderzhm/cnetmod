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

import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.protocol.http.semantics;

namespace cnetmod {

/**
 * @brief Machine-readable cause of an authentication failure.
 *
 * Applications map it to their own error codes instead of matching message
 * text. Authenticators set it on the failures they return.
 */
export enum class jwt_auth_reason
{
    /// No credential was presented on a route that requires one.
    missing_credentials,
    /// The header does not carry the expected prefix or token.
    malformed_credentials,
    /// The token or the session it references is not valid.
    invalid_credentials,
    /// The token is well formed but past its expiry.
    expired_credentials,
    /// Verification could not complete (session store, principal source).
    unavailable,
};

export struct jwt_auth_failure
{
    int status = http::status::unauthorized;
    std::string message = "invalid or expired token";
    jwt_auth_reason reason = jwt_auth_reason::invalid_credentials;
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

export auto jwt_auth(jwt_auth_options opts) -> http::middleware_fn;

} // namespace cnetmod
