/**
 * @file cache_store.cppm
 * @brief 缓存存储抽象接口 — 轻量级模块，不引入具体后端依赖
 *
 * 从 cache.cppm 拆出，仅包含 cache_store 抽象基类。
 * 需要缓存接口但不需要 memory_cache/redis_cache 实现的模块
 * （如 ip_firewall）可只 import 此模块，避免传递性引入
 * cnetmod.protocol.redis 等重量级依赖，防止 MSVC C1605。
 *
 * 使用示例:
 *   import cnetmod.middleware.cache_store;
 *
 *   void foo(cnetmod::cache::cache_store& store) { ... }
 */
export module cnetmod.middleware.cache_store;

import std;
import cnetmod.coro.task;

namespace cnetmod::cache {

// =============================================================================
// cache_store — 缓存存储抽象接口
// =============================================================================

export class cache_store {
public:
    virtual ~cache_store() = default;

    /// 获取缓存值，不存在或已过期返回 nullopt
    virtual auto get(std::string_view key)
        -> task<std::optional<std::string>> = 0;

    /// 设置缓存值，ttl = 0 表示不过期
    virtual auto set(std::string_view key, std::string_view value,
                     std::chrono::seconds ttl = std::chrono::seconds{0})
        -> task<bool> = 0;

    /// 删除缓存
    virtual auto del(std::string_view key) -> task<bool> = 0;

    /// 检查 key 是否存在
    virtual auto exists(std::string_view key) -> task<bool> = 0;
};

} // namespace cnetmod::cache
