/**
 * @file cache.cppm
 * @brief cnetmod 缓存框架 — Spring Cache 风格的声明式缓存
 *
 * 提供统一的 cache_store 接口，支持本地内存 (memory_cache) 和 Redis (redis_cache)
 * 两种后端，并通过 handler 装饰器实现类似 Spring 的 @Cacheable / @CachePut / @CacheEvict
 * 切面效果。支持缓存分组（group）、条件缓存（condition/unless）、分组失效等高级特性。
 *
 * 使用示例:
 *   import cnetmod.middleware.cache;
 *
 *   // 创建缓存后端 + 分组注册表
 *   cnetmod::cache::memory_cache store({.max_entries = 5000});
 *   cnetmod::cache::cache_group_registry groups;
 *
 *   // @Cacheable: 命中缓存直接返回，未命中执行 handler 后缓存结果
 *   router.get("/api/users/:id",
 *       cache::cacheable(store, {.key_pattern = "user:{id}",
 *                                .ttl = 60s,
 *                                .group = "users"}, get_user_handler, &groups));
 *
 *   // @CachePut: 始终执行 handler，然后更新缓存
 *   router.put("/api/users/:id",
 *       cache::cache_put(store, {.key_pattern = "user:{id}",
 *                                .ttl = 60s,
 *                                .group = "users"}, update_user_handler, &groups));
 *
 *   // @CacheEvict: 执行 handler 后删除指定缓存
 *   router.del("/api/users/:id",
 *       cache::cache_evict(store, {.key_pattern = "user:{id}",
 *                                  .group = "users"}, delete_user_handler, &groups));
 *
 *   // @CacheEvict(allEntries=true): 修改数据后使整组缓存失效
 *   router.post("/api/users/batch",
 *       cache::cache_evict_group(store, groups, "users", batch_update_handler));
 *
 *   // 条件缓存: 仅当 id > 0 时缓存, 响应含 error 字段时不缓存
 *   router.get("/api/items/:id",
 *       cache::cacheable(store, {.key_pattern = "item:{id}", .ttl = 30s,
 *           .condition = [](auto& ctx) { return ctx.param("id") != "0"; },
 *           .unless = [](auto& ctx) {
 *               return ctx.resp().body().find("error") != std::string_view::npos;
 *           }
 *       }, get_item_handler));
 *
 *   // Redis 后端
 *   cnetmod::cache::redis_cache rstore(redis_client, {.key_prefix = "myapp:"});
 *   router.get("/api/posts/:id",
 *       cache::cacheable(rstore, {.key_pattern = "post:{id}", .ttl = 120s},
 *                        get_post_handler));
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.middleware.cache;

export import cnetmod.middleware.cache_store;  // re-export cache_store 接口

import std;
import cnetmod.coro.task;
import cnetmod.coro.shared_mutex;
import cnetmod.protocol.redis;
import cnetmod.protocol.http;

namespace cnetmod::cache {

// =============================================================================
// memory_cache — 本地内存缓存 (LRU + TTL, 协程读写锁)
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
        // 先用读锁尝试查找
        co_await rw_.lock_shared();
        async_shared_lock_guard rg(rw_, std::adopt_lock);

        auto it = map_.find(std::string(key));
        if (it == map_.end())
            co_return std::nullopt;

        auto& entry = it->second;

        // 检查过期 — 过期时需要写操作，但这里先返回 nullopt
        // 惰性删除：过期条目在下次 set/del 时清理
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
            // 已存在: 更新
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

        // 容量检查: 淘汰最久未用（同时清理过期条目）
        while (map_.size() >= opts_.max_entries && !lru_.empty()) {
            auto& evict_key = lru_.back();
            map_.erase(evict_key);
            lru_.pop_back();
        }

        // 插入新条目
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

        // 惰性过期检查
        if (it->second.has_expiry && clock::now() >= it->second.expire_at)
            co_return false;

        co_return true;
    }

    /// 获取当前缓存条目数
    auto size() -> task<std::size_t> {
        co_await rw_.lock_shared();
        async_shared_lock_guard rg(rw_, std::adopt_lock);
        co_return map_.size();
    }

    /// 清空所有缓存
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
    async_shared_mutex rw_;   // 协程读写锁：读多写少场景最优
    std::unordered_map<std::string, cache_entry> map_;
    std::list<std::string> lru_;   // front = 最近使用, back = 最久未用
};

// =============================================================================
// redis_cache — Redis 缓存后端
// =============================================================================

export struct redis_cache_options {
    std::string key_prefix;   // 命名空间隔离, 如 "myapp:"
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
// cache_group_registry — 缓存分组注册表
// 跟踪每个 group 下有哪些 cache key，用于按组批量失效
// =============================================================================

export class cache_group_registry {
public:
    cache_group_registry() = default;

    /// 将 key 注册到指定 group
    auto add(std::string_view group, std::string_view key) -> task<void> {
        co_await rw_.lock();
        async_unique_lock_guard wg(rw_, std::adopt_lock);
        groups_[std::string(group)].emplace(std::string(key));
    }

    /// 获取 group 下所有 key 的快照
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

    /// 从 group 中移除指定 key
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

    /// 清空整个 group 的所有 key 记录
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
// cache_key_options — 缓存 key 配置 (Spring Cache 注解参数)
// =============================================================================

export struct cache_key_options {
    std::string key_pattern;               // 如 "user:{id}"
    std::chrono::seconds ttl{60};          // 缓存过期时间
    std::string group;                     // 缓存分组, 如 "users"
    bool all_entries = false;              // @CacheEvict(allEntries=true)

    /// 前置条件: 返回 false 时跳过缓存逻辑，直接执行 handler（透传）
    /// 类似 Spring @Cacheable(condition = "...")
    std::function<bool(const http::request_context&)> condition;

    /// 后置排除: handler 执行后若返回 true，则不缓存该结果
    /// 类似 Spring @Cacheable(unless = "...")
    std::function<bool(const http::request_context&)> unless;
};

// =============================================================================
// 内部: key 解析 — 将 {param} 占位符替换为路由参数值
// =============================================================================

namespace detail {

/// 从 request_context 解析 key_pattern 中的 {param} 占位符
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
                    // 参数不存在, 用 query_string 中的值或原样保留
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

/// 缓存条目序列化: content-type + '\n' + body
/// Content-Type 不含换行符，第一个 '\n' 作为分隔符
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

/// 反序列化缓存条目
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

/// 批量删除 group 下所有 key 并清空注册表
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
// cacheable — 类似 Spring @Cacheable
// 命中缓存直接返回，未命中执行 handler 后缓存结果
// 支持 condition/unless/group
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
        // condition: 不满足则直接透传
        if (opts.condition && !opts.condition(ctx)) {
            co_await handler(ctx);
            co_return;
        }

        auto key = detail::resolve_key(opts.key_pattern, ctx);

        // 1. 查缓存
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

        // 2. 未命中: 执行原始 handler
        co_await handler(ctx);

        // unless: handler 执行后判断是否排除缓存
        if (opts.unless && opts.unless(ctx)) {
            ctx.resp().set_header("X-Cache", "SKIP");
            co_return;
        }

        // 3. 仅缓存 2xx 成功响应
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300) {
            auto ct = ctx.resp().get_header("Content-Type");
            auto body = ctx.resp().body();
            if (!body.empty()) {
                auto entry = detail::serialize_entry(ct, body);
                co_await store.set(key, entry, opts.ttl);

                // 注册到分组
                if (registry && !opts.group.empty())
                    co_await registry->add(opts.group, key);
            }
        }

        ctx.resp().set_header("X-Cache", "MISS");
    };
}

// =============================================================================
// cache_put — 类似 Spring @CachePut
// 始终执行 handler，然后用结果更新缓存
// 支持 condition/unless/group
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
        // condition: 不满足则直接透传
        if (opts.condition && !opts.condition(ctx)) {
            co_await handler(ctx);
            co_return;
        }

        // 1. 始终执行 handler
        co_await handler(ctx);

        // unless: 排除
        if (opts.unless && opts.unless(ctx))
            co_return;

        // 2. 更新缓存
        auto key = detail::resolve_key(opts.key_pattern, ctx);
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300) {
            auto ct = ctx.resp().get_header("Content-Type");
            auto body = ctx.resp().body();
            if (!body.empty()) {
                auto entry = detail::serialize_entry(ct, body);
                co_await store.set(key, entry, opts.ttl);

                // 注册到分组
                if (registry && !opts.group.empty())
                    co_await registry->add(opts.group, key);
            }
        }
    };
}

// =============================================================================
// cache_evict — 类似 Spring @CacheEvict
// 执行 handler 后删除指定缓存
// 支持 all_entries=true 按组清除、condition、group
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
        // condition: 不满足则直接透传
        if (opts.condition && !opts.condition(ctx)) {
            co_await handler(ctx);
            co_return;
        }

        // 1. 执行 handler
        co_await handler(ctx);

        // 2. 按组全部清除 or 单 key 删除
        if (opts.all_entries && registry && !opts.group.empty()) {
            // @CacheEvict(allEntries=true): 清除整组
            co_await detail::evict_all_in_group(store, *registry, opts.group);
        } else {
            auto key = detail::resolve_key(opts.key_pattern, ctx);
            co_await store.del(key);

            // 从分组注册表中移除
            if (registry && !opts.group.empty())
                co_await registry->remove_key(opts.group, key);
        }
    };
}

// =============================================================================
// cache_evict_group — 便捷接口: 使整组缓存失效
// 适用于批量修改接口，一次性清除所有相关缓存
// e.g. POST /api/users/batch → 使 "users" 组全部失效
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
// 全局中间件 — 按路径模式自动缓存 GET 请求
// 适用于需要对一批路由统一加缓存的场景
// 支持 group 注册
// =============================================================================

export struct global_cache_options {
    std::chrono::seconds ttl{60};
    std::string key_prefix = "http:";
    std::string group;     // 可选: 注册到指定分组
    /// 自定义判断是否缓存该请求 (为空则默认只缓存 GET)
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
        // 判断是否需要缓存
        bool should_cache = false;
        if (opts.cacheable_fn) {
            should_cache = opts.cacheable_fn(ctx);
        } else {
            // 默认仅缓存 GET 请求
            should_cache = (ctx.method() == "GET");
        }

        if (!should_cache) {
            co_await next();
            co_return;
        }

        // 生成 key: prefix + path + query
        auto key = opts.key_prefix + std::string(ctx.path());
        auto qs = ctx.query_string();
        if (!qs.empty()) {
            key += '?';
            key.append(qs);
        }

        // 查缓存
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

        // 未命中: 继续执行
        co_await next();

        // 缓存 2xx 响应
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300) {
            auto ct = ctx.resp().get_header("Content-Type");
            auto body = ctx.resp().body();
            if (!body.empty()) {
                auto entry = detail::serialize_entry(ct, body);
                co_await store.set(key, entry, opts.ttl);

                // 注册到分组
                if (registry && !opts.group.empty())
                    co_await registry->add(opts.group, key);
            }
        }

        ctx.resp().set_header("X-Cache", "MISS");
    };
}

} // namespace cnetmod::cache
