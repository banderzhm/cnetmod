module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http.middleware.cache;
// Protocol-qualified filename prevents duplicate source basenames.

export import cnetmod.protocol.http.middleware.cache_store;

import std;
import cnetmod.coro.task;
import cnetmod.coro.shared_mutex;
import cnetmod.protocol.redis;
import cnetmod.protocol.http;

export namespace cnetmod::cache {

struct memory_cache_options {
  std::size_t max_entries = 10000;
};

class memory_cache : public cache_store {
public:
  explicit memory_cache(memory_cache_options opts = {}) noexcept;
  auto get(std::string_view key) -> task<std::optional<std::string>> override;
  auto set(std::string_view key, std::string_view value,
           std::chrono::seconds ttl) -> task<bool> override;
  auto del(std::string_view key) -> task<bool> override;
  auto exists(std::string_view key) -> task<bool> override;
  auto size() -> task<std::size_t>;
  auto clear() -> task<void>;

private:
  using clock = std::chrono::steady_clock;
  struct cache_entry {
    std::string value;
    bool has_expiry = false;
    clock::time_point expire_at{};
    std::list<std::string>::iterator lru_it;
  };
  memory_cache_options opts_;
  async_shared_mutex rw_;
  std::unordered_map<std::string, cache_entry> map_;
  std::list<std::string> lru_;
};

struct redis_cache_options {
  std::string key_prefix;
};

class redis_cache : public cache_store {
public:
  explicit redis_cache(redis::client &client,
                       redis_cache_options opts = {}) noexcept;
  auto get(std::string_view key) -> task<std::optional<std::string>> override;
  auto set(std::string_view key, std::string_view value,
           std::chrono::seconds ttl) -> task<bool> override;
  auto del(std::string_view key) -> task<bool> override;
  auto exists(std::string_view key) -> task<bool> override;

private:
  auto full_key(std::string_view key) const -> std::string;
  redis::client &client_;
  redis_cache_options opts_;
};

class cache_group_registry {
public:
  cache_group_registry() = default;
  auto add(std::string_view group, std::string_view key) -> task<void>;
  auto keys(std::string_view group) -> task<std::vector<std::string>>;
  auto remove_key(std::string_view group, std::string_view key) -> task<void>;
  auto clear_group(std::string_view group) -> task<void>;

private:
  async_shared_mutex rw_;
  std::unordered_map<std::string, std::unordered_set<std::string>> groups_;
};

struct cache_key_options {
  std::string key_pattern;
  std::chrono::seconds ttl{60};
  std::string group;
  bool all_entries = false;
  std::function<bool(const http::request_context &)> condition;
  std::function<bool(const http::request_context &)> unless;
};

[[nodiscard]] auto cacheable(cache_store &store, cache_key_options opts,
                             http::handler_fn handler,
                             cache_group_registry *registry = nullptr)
    -> http::handler_fn;
[[nodiscard]] auto cache_put(cache_store &store, cache_key_options opts,
                             http::handler_fn handler,
                             cache_group_registry *registry = nullptr)
    -> http::handler_fn;
[[nodiscard]] auto cache_evict(cache_store &store, cache_key_options opts,
                               http::handler_fn handler,
                               cache_group_registry *registry = nullptr)
    -> http::handler_fn;
[[nodiscard]] auto
cache_evict_group(cache_store &store, cache_group_registry &registry,
                  std::string group_name, http::handler_fn handler)
    -> http::handler_fn;

struct global_cache_options {
  std::chrono::seconds ttl{60};
  std::string key_prefix = "http:";
  std::string group;
  std::function<bool(const http::request_context &)> cacheable_fn;
};

[[nodiscard]] auto
make_cache_middleware(cache_store &store, global_cache_options opts = {},
                      cache_group_registry *registry = nullptr)
    -> http::middleware_fn;
} // namespace cnetmod::cache
