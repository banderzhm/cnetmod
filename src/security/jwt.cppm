/**
 * @file jwt.cppm
 * @brief Coroutine-native JWT sign/verify module — backed by jwt-cpp
 *
 * Provides non-blocking JWT operations by offloading CPU-intensive
 * cryptographic work to the stdexec thread pool via blocking_invoke.
 *
 * Usage:
 *   import cnetmod.security.jwt;
 *
 *   // Sign
 *   auto token = co_await sign_jwt(pool, io, {
 *       .issuer = "myapp", .subject = "user123",
 *       .scopes = {"read", "write"},
 *       .lifetime = std::chrono::hours(1)
 *   }, "super-secret-key");
 *
 *   // Verify
 *   auto claims = co_await verify_jwt(pool, io, token_value, "super-secret-key");
 *   if (claims && !is_jwt_expired(*claims)) { ... }
 */
export module cnetmod.security.jwt;

import std;
import cnetmod.coro.task;
import cnetmod.executor.pool;
import cnetmod.io.io_context;

export namespace cnetmod::security {

// =============================================================================
// jwt_algorithm — Supported signing algorithms
// =============================================================================

enum class jwt_algorithm
{
    hs256, ///< HMAC-SHA256 (symmetric)
    // rs256, ///< RSA-SHA256 (asymmetric) — future
};

// =============================================================================
// jwt_claims — Parsed JWT claims
// =============================================================================

struct jwt_claims
{
    std::string subject;
    std::string issuer;
    std::vector<std::string> scopes;
    std::chrono::system_clock::time_point issued_at;
    std::chrono::system_clock::time_point expires_at;
    /// All non-standard claims (key → string value)
    std::map<std::string, std::string> custom;
};

// =============================================================================
// jwt_sign_options — Parameters for JWT signing
// =============================================================================

struct jwt_sign_options
{
    std::string issuer;
    std::string subject;
    std::vector<std::string> scopes;
    std::chrono::system_clock::duration lifetime = std::chrono::hours(1);
    jwt_algorithm algorithm = jwt_algorithm::hs256;
    /// Optional additional claims (key → string value) injected into payload
    std::map<std::string, std::string> custom_claims;
};

// =============================================================================
// sign_jwt — Sign a JWT (CPU-intensive, offloaded to thread pool)
// =============================================================================

/// Build and sign a JWT token string.
/// @param pool   stdexec thread pool for offloading
/// @param io     io_context to return to after completion
/// @param opts   Signing parameters (issuer, subject, lifetime, etc.)
/// @param secret HS256 secret key (or PEM private key for future RS256)
/// @return Signed JWT string on success, error message on failure
auto sign_jwt(thread_pool& pool, io_context& io,
              const jwt_sign_options& opts, std::string_view secret)
    -> task<std::expected<std::string, std::string>>;

// =============================================================================
// verify_jwt — Verify and parse a JWT (CPU-intensive, offloaded)
// =============================================================================

/// Verify a JWT signature and extract claims.
/// @param pool   stdexec thread pool for offloading
/// @param io     io_context to return to after completion
/// @param token  Encoded JWT string (header.payload.signature)
/// @param secret HS256 secret key used for verification
/// @return Parsed jwt_claims on success, error message on failure
auto verify_jwt(thread_pool& pool, io_context& io,
                std::string_view token, std::string_view secret)
    -> task<std::expected<jwt_claims, std::string>>;

// =============================================================================
// is_jwt_expired — Lightweight expiry check (no crypto)
// =============================================================================

/// Check whether the token has expired relative to the current system clock.
[[nodiscard]] inline auto is_jwt_expired(const jwt_claims& claims) -> bool
{
    return std::chrono::system_clock::now() > claims.expires_at;
}

} // namespace cnetmod::security
