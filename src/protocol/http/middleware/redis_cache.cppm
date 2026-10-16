/**
 * @brief Redis-backed adapter for the protocol-neutral HTTP cache middleware.
 */
export module cnetmod.integration.http.redis_cache;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http.middleware.cache_store;
import cnetmod.protocol.redis;

export namespace cnetmod::cache {

struct redis_cache_options
{
    std::string key_prefix;
};

class redis_cache final : public cache_store
{
public:
    explicit redis_cache(redis::client& client,
        redis_cache_options opts = {}) noexcept;
    auto get(std::string_view key) -> task<std::optional<std::string>> override;
    auto set(std::string_view key, std::string_view value,
        std::chrono::seconds ttl) -> task<bool> override;
    auto del(std::string_view key) -> task<bool> override;
    auto exists(std::string_view key) -> task<bool> override;

private:
    [[nodiscard]] auto full_key(std::string_view key) const -> std::string;

    redis::client& client_;
    redis_cache_options opts_;
};

} // namespace cnetmod::cache
