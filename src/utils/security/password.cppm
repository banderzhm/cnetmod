/**
 * @file password.cppm
 * @brief Password hashing with PBKDF2-HMAC-SHA256 and self-describing hashes.
 */
export module cnetmod.security.password;

import std;
import cnetmod.coro.task;
import cnetmod.executor.pool;
import cnetmod.io.io_context;

export namespace cnetmod::security {

enum class password_hash_algorithm
{
    pbkdf2_sha256,
};

struct password_hash_options
{
    password_hash_algorithm algorithm = password_hash_algorithm::pbkdf2_sha256;
    std::uint32_t iterations = 210000;
    std::size_t salt_bytes = 16;
    std::size_t digest_bytes = 32;
};

/** Hash a non-empty password using a fresh CSPRNG salt. */
[[nodiscard]] auto hash_password(std::string_view password,
    password_hash_options options = {})
    -> std::expected<std::string, std::error_code>;

/** Verify a self-describing password hash in constant time. */
[[nodiscard]] auto verify_password(std::string_view password,
    std::string_view encoded) noexcept -> bool;

/** Report whether a valid hash should be regenerated with current policy. */
[[nodiscard]] auto password_hash_needs_rehash(std::string_view encoded,
    password_hash_options options = {}) noexcept -> bool;

/** CPU-pool overload that resumes on the supplied request event loop. */
auto hash_password(thread_pool& pool, io_context& io,
    std::string password, password_hash_options options = {})
    -> task<std::expected<std::string, std::error_code>>;

/** CPU-pool overload that resumes on the supplied request event loop. */
auto verify_password(thread_pool& pool, io_context& io,
    std::string password, std::string encoded) -> task<bool>;

} // namespace cnetmod::security
