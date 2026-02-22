/**
 * @file cache.cppm
 * @brief cnetmod cache framework — Spring Cache style declarative caching
 *
 * Provides unified cache_store interface, supports local memory (memory_cache) and Redis (redis_cache)
 * backends, and implements Spring-like @Cacheable / @CachePut / @CacheEvict
 * aspect effects through handler decorators. Supports advanced features like cache grouping (group), conditional caching (condition/unless), and group invalidation.
 *
 * Usage example:
 *   import cnetmod.middleware.cache;
 *
 *   // Create cache backend + group registry
 *   cnetmod::cache::memory_cache store({.max_entries = 5000});
 *   cnetmod::cache::cache_group_registry groups;
 *
 *   // @Cacheable: return cached result if hit, execute handler and cache result if miss
 *   router.get("/api/users/:id",
 *       cache::cacheable(store, {.key_pattern = "user:{id}",
 *                                .ttl = 60s,
 *                                .group = "users"}, get_user_handler, &groups));
 *
 *   // @CachePut: always execute handler, then update cache
 *   router.put("/api/users/:id",
 *       cache::cache_put(store, {.key_pattern = "user:{id}",
 *                                .ttl = 60s,
 *                                .group = "users"}, update_user_handler, &groups));
 *
 *   // @CacheEvict: delete specified cache after executing handler
 *   router.del("/api/users/:id",
 *       cache::cache_evict(store, {.key_pattern = "user:{id}",
 *                                  .group = "users"}, delete_user_handler, &groups));
 *
 *   // @CacheEvict(allEntries=true): invalidate entire group cache after data modification
 *   router.post("/api/users/batch",
 *       cache::cache_evict_group(store, groups, "users", batch_update_handler));
 *
 *   // Conditional caching: cache only when id > 0, don't cache if response contains error field
 *   router.get("/api/items/:id",
 *       cache::cacheable(store, {.key_pattern = "item:{id}", .ttl = 30s,
 *           .condition = [](auto& ctx) { return ctx.param("id") != "0"; },
 *           .unless = [](auto& ctx) {
 *               return ctx.resp().body().find("error") != std::string_view::npos;
 *           }
 *       }, get_item_handler));
 *
 *   // Redis backend
 *   cnetmod::cache::redis_cache rstore(redis_client, {.key_prefix = "myapp:"});
 *   router.get("/api/posts/:id",
 *       cache::cacheable(rstore, {.key_pattern = "post:{id}", .ttl = 120s},
 *                        get_post_handler));
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.middleware.cache;

export import cnetmod.middleware.cache_store;  // re-export cache_store interface

import std;
import cnetmod.coro.task;
import cnetmod.coro.shared_mutex;
import cnetmod.protocol.redis;
import cnetmod.protocol.http;

namespace cnetmod::cache {

// =============================================================================
// memory_cache — Local memory cache (LRU + TTL, coroutine read-write lock)
// =============================================================================

export struct memory_cache_options {
    std::size_t max_entries = 10000;
};

export class memory_cache : public cache_store {
public:
    explicit memory_cache(memory_cache_options opts = {}) noexcept
        : opts_(opts) {}

    auto get(std::string_view key)
        -> task<std::optional<std::string>> override
    {
        // First try to find with read lock
        co_await rw_.lock_shared();
        async_shared_lock_guard rg(rw_, std::adopt_lock);

        auto it = map_.find(std::string(key));
        if (it == map_.end())
            co_return std::nullopt;

        auto& entry = it->second;

        // Check expiry — expired entries need write operation, but return nullopt here first
        // Lazy deletion: expired entries cleaned up on next set/del
        if (entry.has_expiry && clock::now() >= entry.expire_at)
            co_return std::nullopt;

        co_return entry.value;
    }

    auto set(std::string_view key, std::string_view value,
             std::chrono::seconds ttl) -> task<bool> override
    {
        co_await rw_.lock();
        async_unique_lock_guard wg(rw_, std::adopt_lock);

        auto key_str = std::string(key);
        auto it = map_.find(key_str);

        if (it != map_.end()) {
            // Already exists: update
            lru_.erase(it->second.lru_it);
            lru_.push_front(key_str);
            it->second.value = std::string(value);
            it->second.lru_it = lru_.begin();
            if (ttl.count() > 0) {
                it->second.has_expiry = true;
                it->second.expire_at = clock::now() + ttl;
            } else {
                it->second.has_expiry = false;
            }
            co_return true;
        }

        // Capacity check: evict least recently used (also clean expired entries)
        while (map_.size() >= opts_.max_entries && !lru_.empty()) {
            auto& evict_key = lru_.back();
            map_.erase(evict_key);
            lru_.pop_back();
        }

        // Insert new entry
        lru_.push_front(key_str);
        cache_entry entry;
        entry.value = std::string(value);
        entry.lru_it = lru_.begin();
        if (ttl.count() > 0) {
            entry.has_expiry = true;
            entry.expire_at = clock::now() + ttl;
        }

        map_.emplace(std::move(key_str), std::move(entry));
        co_return true;
    }

    auto del(std::string_view key) -> task<bool> override {
        co_await rw_.lock();
        async_unique_lock_guard wg(rw_, std::adopt_lock);

        auto it = map_.find(std::string(key));
        if (it == map_.end())
            co_return false;

        lru_.erase(it->second.lru_it);
        map_.erase(it);
        co_return true;
    }

    auto exists(std::string_view key) -> task<bool> override {
        co_await rw_.lock_shared();
        async_shared_lock_guard rg(rw_, std::adopt_lock);

        auto it = map_.find(std::string(key));
        if (it == map_.end())
            co_return false;

        // Lazy expiry check
        if (it->second.has_expiry && clock::now() >= it->second.expire_at)
            co_return false;

        co_return true;
    }

    /// Get current cache entry count
    auto size() -> task<std::size_t> {
        co_await rw_.lock_shared();
        async_shared_lock_guard rg(rw_, std::adopt_lock);
        co_return map_.size();
    }

    /// Clear all cache
    auto clear() -> task<void> {
        co_await rw_.lock();
        async_unique_lock_guard wg(rw_, std::adopt_lock);
        map_.clear();
        lru_.clear();
    }

private:
    using clock = std::chrono::steady_clock;

    struct cache_entry {
        std::string value;
        bool has_expiry = false;
        clock::time_point expire_at{};
        std::list<std::string>::iterator lru_it;
    };

    memory_cache_options opts_;
    async_shared_mutex rw_;   // Coroutine read-write lock: optimal for read-heavy scenarios
    std::unordered_map<std::string, cache_entry> map_;
    std::list<std::string> lru_;   // front = most recently used, back = least recently used
};

// =============================================================================
// redis_cache — Redis cache backend
// =============================================================================

export struct redis_cache_options {
    std::string key_prefix;   // Namespace isolation, e.g. "myapp:"
};

export class redis_cache : public cache_store {
public:
    explicit redis_cache(redis::client& client,
                         redis_cache_options opts = {}) noexcept
        : client_(client), opts_(std::move(opts)) {}

    auto get(std::string_view key)
        -> task<std::optional<std::string>> override
    {
        auto fk = full_key(key);
        redis::request req;
        req.push("GET", fk);
        auto r = co_await client_.exec(req);
        if (!r || r->empty() || r->front().is_null() || r->front().is_error())
            co_return std::nullopt;
        co_return std::string(redis::first_value(*r));
    }

    auto set(std::string_view key, std::string_view value,
             std::chrono::seconds ttl) -> task<bool> override
    {
        auto fk = full_key(key);
        auto val = std::string(value);
        redis::request req;
        if (ttl.count() > 0) {
            req.push("SET", fk, val, std::string("EX"),
                     std::to_string(ttl.count()));
        } else {
            req.push("SET", fk, val);
        }
        auto r = co_await client_.exec(req);
        co_return r.has_value() && redis::is_ok(*r);
    }

    auto del(std::string_view key) -> task<bool> override {
        auto fk = full_key(key);
        redis::request req;
        req.push("DEL", fk);
        auto r = co_await client_.exec(req);
        if (!r || r->empty())
            co_return false;
        co_return redis::first_value(*r) != "0";
    }

    auto exists(std::string_view key) -> task<bool> override {
        auto fk = full_key(key);
        redis::request req;
        req.push("EXISTS", fk);
        auto r = co_await client_.exec(req);
        if (!r || r->empty())
            co_return false;
        co_return redis::first_value(*r) != "0";
    }

private:
    auto full_key(std::string_view key) const -> std::string {
        return opts_.key_prefix + std::string(key);
    }

    redis::client& client_;
    redis_cache_options opts_;
};

// =============================================================================
// cache_group_registry — Cache group registry
// Tracks which cache keys belong to each group, for batch invalidation by group
// =============================================================================

export class cache_group_registry {
public:
    cache_group_registry() = default;

    /// Register key to specified group
    auto add(std::string_view group, std::string_view key) -> task<void> {
        co_await rw_.lock();
        async_unique_lock_guard wg(rw_, std::adopt_lock);
        groups_[std::string(group)].emplace(std::string(key));
    }

    /// Get snapshot of all keys under group
    auto keys(std::string_view group) -> task<std::vector<std::string>> {
        co_await rw_.lock_shared();
        async_shared_lock_guard rg(rw_, std::adopt_lock);

        auto it = groups_.find(std::string(group));
        if (it == groups_.end())
            co_return std::vector<std::string>{};

        std::vector<std::string> result;
        result.reserve(it->second.size());
        for (auto& k : it->second)
            result.push_back(k);
        co_return result;
    }

    /// Remove specified key from group
    auto remove_key(std::string_view group, std::string_view key) -> task<void> {
        co_await rw_.lock();
        async_unique_lock_guard wg(rw_, std::adopt_lock);

        auto it = groups_.find(std::string(group));
        if (it != groups_.end()) {
            it->second.erase(std::string(key));
            if (it->second.empty())
                groups_.erase(it);
        }
    }

    /// Clear all key records for entire group
    auto clear_group(std::string_view group) -> task<void> {
        co_await rw_.lock();
        async_unique_lock_guard wg(rw_, std::adopt_lock);
        groups_.erase(std::string(group));
    }

private:
    async_shared_mutex rw_;
    std::unordered_map<std::string, std::unordered_set<std::string>> groups_;
};

// =============================================================================
// cache_key_options — Cache key configuration (Spring Cache annotation parameters)
// =============================================================================

export struct cache_key_options {
    std::string key_pattern;               // e.g. "user:{id}"
    std::chrono::seconds ttl{60};          // Cache expiration time
    std::string group;                     // Cache group, e.g. "users"
    bool all_entries = false;              // @CacheEvict(allEntries=true)

    /// Precondition: skip cache logic and execute handler directly (pass-through) when returns false
    /// Similar to Spring @Cacheable(condition = "...")
    std::function<bool(const http::request_context&)> condition;

    /// Post-exclusion: don't cache the result if returns true after handler execution
    /// Similar to Spring @Cacheable(unless = "...")
    std::function<bool(const http::request_context&)> unless;
};

// =============================================================================
// Internal: key resolution — replace {param} placeholders with route parameter values
// =============================================================================

namespace detail {

/// Parse {param} placeholders in key_pattern from request_context
/// e.g. "user:{id}:detail" + ctx.param("id")=="42" → "user:42:detail"
inline auto resolve_key(const std::string& pattern,
                        const http::request_context& ctx) -> std::string
{
    std::string result;
    result.reserve(pattern.size());

    std::size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '{') {
            auto close = pattern.find('}', i + 1);
            if (close != std::string::npos) {
                auto name = std::string_view(pattern).substr(i + 1, close - i - 1);
                auto val = ctx.param(name);
                if (!val.empty()) {
                    result.append(val);
                } else {
                    // Parameter doesn't exist, use value from query_string or keep as-is
                    result += '{';
                    result.append(name);
                    result += '}';
                }
                i = close + 1;
                continue;
            }
        }
        result += pattern[i];
        ++i;
    }

    return result;
}

/// Cache entry serialization: content-type + '\n' + body
/// Content-Type doesn't contain newlines, first '\n' serves as delimiter
inline auto serialize_entry(std::string_view content_type,
                            std::string_view body) -> std::string
{
    std::string entry;
    entry.reserve(content_type.size() + 1 + body.size());
    entry.append(content_type);
    entry += '\n';
    entry.append(body);
    return entry;
}

/// Deserialize cache entry
struct cached_response {
    std::string content_type;
    std::string body;
};

inline auto deserialize_entry(std::string_view data)
    -> std::optional<cached_response>
{
    auto pos = data.find('\n');
    if (pos == std::string_view::npos)
        return std::nullopt;
    return cached_response{
        std::string(data.substr(0, pos)),
        std::string(data.substr(pos + 1)),
    };
}

/// Batch delete all keys under group and clear registry
inline auto evict_all_in_group(cache_store& store,
                               cache_group_registry& registry,
                               std::string_view group) -> task<void>
{
    auto all_keys = co_await registry.keys(group);
    for (auto& k : all_keys)
        co_await store.del(k);
    co_await registry.clear_group(group);
}

} // namespace detail

// =============================================================================
// cacheable — similar to Spring @Cacheable
// Return cached result if hit, execute handler and cache result if miss
// Supports condition/unless/group
// =============================================================================

export auto cacheable(cache_store& store, cache_key_options opts,
                      http::handler_fn handler,
                      cache_group_registry* registry = nullptr)
    -> http::handler_fn
{
    return [&store, opts = std::move(opts),
            handler = std::move(handler), registry](
        http::request_context& ctx) -> task<void>
    {
        // condition: skip caching if not satisfied
        if (opts.condition && !opts.condition(ctx)) {
            co_await handler(ctx);
            co_return;
        }

        auto key = detail::resolve_key(opts.key_pattern, ctx);

        // 1. Check cache
        auto cached = co_await store.get(key);
        if (cached) {
            auto entry = detail::deserialize_entry(*cached);
            if (entry) {
                ctx.resp().set_status(http::status::ok);
                ctx.resp().set_header("Content-Type", entry->content_type);
                ctx.resp().set_header("X-Cache", "HIT");
                ctx.resp().set_body(std::move(entry->body));
                co_return;
            }
        }

        // 2. Cache miss: execute original handler
        co_await handler(ctx);

        // unless: check if caching should be skipped after handler execution
        if (opts.unless && opts.unless(ctx)) {
            ctx.resp().set_header("X-Cache", "SKIP");
            co_return;
        }

        // 3. Only cache 2xx successful responses
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300) {
            auto ct = ctx.resp().get_header("Content-Type");
            auto body = ctx.resp().body();
            if (!body.empty()) {
                auto entry = detail::serialize_entry(ct, body);
                co_await store.set(key, entry, opts.ttl);

                // Register to group
                if (registry && !opts.group.empty())
                    co_await registry->add(opts.group, key);
            }
        }

        ctx.resp().set_header("X-Cache", "MISS");
    };
}

// =============================================================================
// cache_put — similar to Spring @CachePut
// Always execute handler, then update cache with result
// Supports condition/unless/group
// =============================================================================

export auto cache_put(cache_store& store, cache_key_options opts,
                      http::handler_fn handler,
                      cache_group_registry* registry = nullptr)
    -> http::handler_fn
{
    return [&store, opts = std::move(opts),
            handler = std::move(handler), registry](
        http::request_context& ctx) -> task<void>
    {
        // condition: skip if not satisfied
        if (opts.condition && !opts.condition(ctx)) {
            co_await handler(ctx);
            co_return;
        }

        // 1. Always execute handler
        co_await handler(ctx);

        // unless: skip caching
        if (opts.unless && opts.unless(ctx))
            co_return;

        // 2. Update cache
        auto key = detail::resolve_key(opts.key_pattern, ctx);
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300) {
            auto ct = ctx.resp().get_header("Content-Type");
            auto body = ctx.resp().body();
            if (!body.empty()) {
                auto entry = detail::serialize_entry(ct, body);
                co_await store.set(key, entry, opts.ttl);

                // Register to group
                if (registry && !opts.group.empty())
                    co_await registry->add(opts.group, key);
            }
        }
    };
}

// =============================================================================
// cache_evict — similar to Spring @CacheEvict
// Delete specified cache after executing handler
// Supports all_entries=true for group-based clearing, condition, group
// =============================================================================

export auto cache_evict(cache_store& store, cache_key_options opts,
                        http::handler_fn handler,
                        cache_group_registry* registry = nullptr)
    -> http::handler_fn
{
    return [&store, opts = std::move(opts),
            handler = std::move(handler), registry](
        http::request_context& ctx) -> task<void>
    {
        // condition: skip if not satisfied
        if (opts.condition && !opts.condition(ctx)) {
            co_await handler(ctx);
            co_return;
        }

        // 1. Execute handler
        co_await handler(ctx);

        // 2. Clear entire group or delete single key
        if (opts.all_entries && registry && !opts.group.empty()) {
            // @CacheEvict(allEntries=true): clear entire group
            co_await detail::evict_all_in_group(store, *registry, opts.group);
        } else {
            auto key = detail::resolve_key(opts.key_pattern, ctx);
            co_await store.del(key);

            // Remove from group registry
            if (registry && !opts.group.empty())
                co_await registry->remove_key(opts.group, key);
        }
    };
}

// =============================================================================
// cache_evict_group — convenience interface: invalidate entire cache group
// Suitable for batch modification endpoints, clear all related caches at once
// e.g. POST /api/users/batch → invalidate entire "users" group
// =============================================================================

export auto cache_evict_group(cache_store& store,
                              cache_group_registry& registry,
                              std::string group_name,
                              http::handler_fn handler) -> http::handler_fn
{
    return [&store, &registry, group_name = std::move(group_name),
            handler = std::move(handler)](
        http::request_context& ctx) -> task<void>
    {
        co_await handler(ctx);
        co_await detail::evict_all_in_group(store, registry, group_name);
    };
}

// =============================================================================
// Global middleware — automatically cache GET requests by path pattern
// Suitable for scenarios requiring unified caching for a batch of routes
// Supports group registration
// =============================================================================

export struct global_cache_options {
    std::chrono::seconds ttl{60};
    std::string key_prefix = "http:";
    std::string group;     // Optional: register to specified group
    /// Custom function to determine if request should be cached (defaults to GET only if empty)
    std::function<bool(const http::request_context&)> cacheable_fn;
};

export auto make_cache_middleware(cache_store& store,
                                 global_cache_options opts = {},
                                 cache_group_registry* registry = nullptr)
    -> http::middleware_fn
{
    return [&store, opts = std::move(opts), registry](
        http::request_context& ctx, http::next_fn next) -> task<void>
    {
        // Determine if caching is needed
        bool should_cache = false;
        if (opts.cacheable_fn) {
            should_cache = opts.cacheable_fn(ctx);
        } else {
            // Default: only cache GET requests
            should_cache = (ctx.method() == "GET");
        }

        if (!should_cache) {
            co_await next();
            co_return;
        }

        // Generate key: prefix + path + query
        auto key = opts.key_prefix + std::string(ctx.path());
        auto qs = ctx.query_string();
        if (!qs.empty()) {
            key += '?';
            key.append(qs);
        }

        // Check cache
        auto cached = co_await store.get(key);
        if (cached) {
            auto entry = detail::deserialize_entry(*cached);
            if (entry) {
                ctx.resp().set_status(http::status::ok);
                ctx.resp().set_header("Content-Type", entry->content_type);
                ctx.resp().set_header("X-Cache", "HIT");
                ctx.resp().set_body(std::move(entry->body));
                co_return;
            }
        }

        // Cache miss: continue execution
        co_await next();

        // Cache 2xx responses
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300) {
            auto ct = ctx.resp().get_header("Content-Type");
            auto body = ctx.resp().body();
            if (!body.empty()) {
                auto entry = detail::serialize_entry(ct, body);
                co_await store.set(key, entry, opts.ttl);

                // Register to group
                if (registry && !opts.group.empty())
                    co_await registry->add(opts.group, key);
            }
        }

        ctx.resp().set_header("X-Cache", "MISS");
    };
}

} // namespace cnetmod::cache
