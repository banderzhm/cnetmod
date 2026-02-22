/**
 * @file cache_store.cppm
 * @brief Cache storage abstract interface — lightweight module, no concrete backend dependencies
 *
 * Extracted from cache.cppm, contains only cache_store abstract base class.
 * Modules that need cache interface but not memory_cache/redis_cache implementations
 * (like ip_firewall) can import only this module, avoiding transitive inclusion of
 * cnetmod.protocol.redis and other heavyweight dependencies, preventing MSVC C1605.
 *
 * Usage example:
 *   import cnetmod.middleware.cache_store;
 *
 *   void foo(cnetmod::cache::cache_store& store) { ... }
 */
export module cnetmod.middleware.cache_store;

import std;
import cnetmod.coro.task;

namespace cnetmod::cache {

// =============================================================================
// cache_store — Cache storage abstract interface
// =============================================================================

export class cache_store {
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
