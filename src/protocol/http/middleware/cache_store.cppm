/**
 * @file cache_store.cppm
 * @brief Cache storage abstract interface — lightweight module, no concrete backend dependencies
 *
 * Extracted from cache.cppm, contains only cache_store abstract base class.
 * Modules that need only the cache contract (like ip_firewall) can import this
 * module without selecting a backend. memory_cache remains the HTTP-local
 * backend; the Redis-backed adapter is an independent integration component
 * and never becomes a transitive dependency of HTTP.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.cache_store;
 *
 *   void foo(cnetmod::cache::cache_store& store) { ... }
 */
export module cnetmod.protocol.http.middleware.cache_store;

import std;
import cnetmod.coro.task;

namespace cnetmod::cache {

// =============================================================================
// cache_store — Cache storage abstract interface
// =============================================================================

export class cache_store
{
public:
    virtual ~cache_store() = default;

    /// Get cached value, returns nullopt if not exists or expired
    virtual auto get(std::string_view key)
        -> task<std::optional<std::string>> = 0;

    /// Set cached value, ttl = 0 means no expiration
    virtual auto set(std::string_view key, std::string_view value,
        std::chrono::seconds ttl = std::chrono::seconds{0})
        -> task<bool> = 0;

    /// Delete cache
    virtual auto del(std::string_view key) -> task<bool> = 0;

    /// Check if key exists
    virtual auto exists(std::string_view key) -> task<bool> = 0;
};

} // namespace cnetmod::cache
