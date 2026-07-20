module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.middleware.cache;

import std;
import cnetmod.coro.task;
import cnetmod.coro.shared_mutex;
import cnetmod.protocol.redis;
import cnetmod.protocol.http;

namespace cnetmod::cache {

memory_cache::memory_cache(memory_cache_options opts) noexcept : opts_(opts) {}

auto memory_cache::get(std::string_view key)
    -> task<std::optional<std::string>> {
  co_await rw_.lock_shared();
  async_shared_lock_guard rg(rw_, std::adopt_lock);
  auto it = map_.find(std::string(key));
  if (it == map_.end() ||
      (it->second.has_expiry && clock::now() >= it->second.expire_at))
    co_return std::nullopt;
  co_return it->second.value;
}

auto memory_cache::set(std::string_view key, std::string_view value,
                       std::chrono::seconds ttl) -> task<bool> {
  co_await rw_.lock();
  async_unique_lock_guard wg(rw_, std::adopt_lock);
  auto key_str = std::string(key);
  auto it = map_.find(key_str);
  if (it != map_.end()) {
    lru_.erase(it->second.lru_it);
    lru_.push_front(key_str);
    it->second.value = std::string(value);
    it->second.lru_it = lru_.begin();
    it->second.has_expiry = ttl.count() > 0;
    if (it->second.has_expiry)
      it->second.expire_at = clock::now() + ttl;
    co_return true;
  }
  while (map_.size() >= opts_.max_entries && !lru_.empty()) {
    map_.erase(lru_.back());
    lru_.pop_back();
  }
  lru_.push_front(key_str);
  cache_entry entry{.value = std::string(value),
                    .has_expiry = ttl.count() > 0,
                    .expire_at = ttl.count() > 0 ? clock::now() + ttl
                                                 : clock::time_point{},
                    .lru_it = lru_.begin()};
  map_.emplace(std::move(key_str), std::move(entry));
  co_return true;
}

auto memory_cache::del(std::string_view key) -> task<bool> {
  co_await rw_.lock();
  async_unique_lock_guard wg(rw_, std::adopt_lock);
  auto it = map_.find(std::string(key));
  if (it == map_.end())
    co_return false;
  lru_.erase(it->second.lru_it);
  map_.erase(it);
  co_return true;
}

auto memory_cache::exists(std::string_view key) -> task<bool> {
  co_await rw_.lock_shared();
  async_shared_lock_guard rg(rw_, std::adopt_lock);
  auto it = map_.find(std::string(key));
  co_return it != map_.end() &&
      (!it->second.has_expiry || clock::now() < it->second.expire_at);
}

auto memory_cache::size() -> task<std::size_t> {
  co_await rw_.lock_shared();
  async_shared_lock_guard rg(rw_, std::adopt_lock);
  co_return map_.size();
}
auto memory_cache::clear() -> task<void> {
  co_await rw_.lock();
  async_unique_lock_guard wg(rw_, std::adopt_lock);
  map_.clear();
  lru_.clear();
}

redis_cache::redis_cache(redis::client &client,
                         redis_cache_options opts) noexcept
    : client_(client), opts_(std::move(opts)) {}
auto redis_cache::full_key(std::string_view key) const -> std::string {
  return opts_.key_prefix + std::string(key);
}
auto redis_cache::get(std::string_view key)
    -> task<std::optional<std::string>> {
  redis::request req;
  req.push("GET", full_key(key));
  auto r = co_await client_.exec(req);
  if (!r || r->empty() || r->front().is_null() || r->front().is_error())
    co_return std::nullopt;
  co_return std::string(redis::first_value(*r));
}
auto redis_cache::set(std::string_view key, std::string_view value,
                      std::chrono::seconds ttl) -> task<bool> {
  auto fk = full_key(key);
  auto val = std::string(value);
  redis::request req;
  if (ttl.count() > 0)
    req.push("SET", fk, val, std::string("EX"), std::to_string(ttl.count()));
  else
    req.push("SET", fk, val);
  auto r = co_await client_.exec(req);
  co_return r.has_value() && redis::is_ok(*r);
}
auto redis_cache::del(std::string_view key) -> task<bool> {
  redis::request req;
  req.push("DEL", full_key(key));
  auto r = co_await client_.exec(req);
  co_return r && !r->empty() && redis::first_value(*r) != "0";
}
auto redis_cache::exists(std::string_view key) -> task<bool> {
  redis::request req;
  req.push("EXISTS", full_key(key));
  auto r = co_await client_.exec(req);
  co_return r && !r->empty() && redis::first_value(*r) != "0";
}

auto cache_group_registry::add(std::string_view group, std::string_view key)
    -> task<void> {
  co_await rw_.lock();
  async_unique_lock_guard wg(rw_, std::adopt_lock);
  groups_[std::string(group)].emplace(std::string(key));
}
auto cache_group_registry::keys(std::string_view group)
    -> task<std::vector<std::string>> {
  co_await rw_.lock_shared();
  async_shared_lock_guard rg(rw_, std::adopt_lock);
  auto it = groups_.find(std::string(group));
  if (it == groups_.end())
    co_return std::vector<std::string>{};
  std::vector<std::string> result;
  result.reserve(it->second.size());
  for (const auto &key : it->second)
    result.push_back(key);
  co_return result;
}
auto cache_group_registry::remove_key(std::string_view group,
                                      std::string_view key) -> task<void> {
  co_await rw_.lock();
  async_unique_lock_guard wg(rw_, std::adopt_lock);
  auto it = groups_.find(std::string(group));
  if (it != groups_.end()) {
    it->second.erase(std::string(key));
    if (it->second.empty())
      groups_.erase(it);
  }
}
auto cache_group_registry::clear_group(std::string_view group) -> task<void> {
  co_await rw_.lock();
  async_unique_lock_guard wg(rw_, std::adopt_lock);
  groups_.erase(std::string(group));
}

namespace {
struct cached_response {
  std::string content_type;
  std::string body;
};
auto resolve_key(const std::string &pattern, const http::request_context &ctx)
    -> std::string {
  std::string result;
  result.reserve(pattern.size());
  for (std::size_t i = 0; i < pattern.size();) {
    if (pattern[i] == '{') {
      auto close = pattern.find('}', i + 1);
      if (close != std::string::npos) {
        auto name = std::string_view(pattern).substr(i + 1, close - i - 1);
        auto value = ctx.param(name);
        if (!value.empty())
          result.append(value);
        else {
          result += '{';
          result.append(name);
          result += '}';
        }
        i = close + 1;
        continue;
      }
    }
    result += pattern[i++];
  }
  return result;
}
auto serialize_entry(std::string_view content_type, std::string_view body)
    -> std::string {
  std::string result;
  result.reserve(content_type.size() + body.size() + 1);
  result.append(content_type);
  result += '\n';
  result.append(body);
  return result;
}
auto deserialize_entry(std::string_view data)
    -> std::optional<cached_response> {
  const auto pos = data.find('\n');
  if (pos == std::string_view::npos)
    return std::nullopt;
  return cached_response{std::string(data.substr(0, pos)),
                         std::string(data.substr(pos + 1))};
}
auto evict_all_in_group(cache_store &store, cache_group_registry &registry,
                        std::string_view group) -> task<void> {
  auto all_keys = co_await registry.keys(group);
  for (auto &key : all_keys)
    co_await store.del(key);
  co_await registry.clear_group(group);
}
auto cache_response(cache_store &store, cache_group_registry *registry,
                    const std::string &group, std::string_view key,
                    std::chrono::seconds ttl, http::request_context &ctx)
    -> task<void> {
  const auto status = ctx.resp().status_code();
  const auto body = ctx.resp().body();
  if (status >= 200 && status < 300 && !body.empty()) {
    auto entry = serialize_entry(ctx.resp().get_header("Content-Type"), body);
    co_await store.set(key, entry, ttl);
    if (registry && !group.empty())
      co_await registry->add(group, key);
  }
}
auto set_cached_response(http::request_context &ctx, std::string_view cached)
    -> bool {
  auto entry = deserialize_entry(cached);
  if (!entry)
    return false;
  ctx.resp().set_status(http::status::ok);
  ctx.resp().set_header("Content-Type", entry->content_type);
  ctx.resp().set_header("X-Cache", "HIT");
  ctx.resp().set_body(std::move(entry->body));
  return true;
}
} // namespace

auto cacheable(cache_store &store, cache_key_options opts,
               http::handler_fn handler, cache_group_registry *registry)
    -> http::handler_fn {
  return [&store, opts = std::move(opts), handler = std::move(handler),
          registry](http::request_context &ctx) -> task<void> {
    if (opts.condition && !opts.condition(ctx)) {
      co_await handler(ctx);
      co_return;
    }
    auto key = resolve_key(opts.key_pattern, ctx);
    auto cached = co_await store.get(key);
    if (cached && set_cached_response(ctx, *cached))
      co_return;
    co_await handler(ctx);
    if (opts.unless && opts.unless(ctx)) {
      ctx.resp().set_header("X-Cache", "SKIP");
      co_return;
    }
    co_await cache_response(store, registry, opts.group, key, opts.ttl, ctx);
    ctx.resp().set_header("X-Cache", "MISS");
  };
}
auto cache_put(cache_store &store, cache_key_options opts,
               http::handler_fn handler, cache_group_registry *registry)
    -> http::handler_fn {
  return [&store, opts = std::move(opts), handler = std::move(handler),
          registry](http::request_context &ctx) -> task<void> {
    if (opts.condition && !opts.condition(ctx)) {
      co_await handler(ctx);
      co_return;
    }
    co_await handler(ctx);
    if (opts.unless && opts.unless(ctx))
      co_return;
    auto key = resolve_key(opts.key_pattern, ctx);
    co_await cache_response(store, registry, opts.group, key, opts.ttl, ctx);
  };
}
auto cache_evict(cache_store &store, cache_key_options opts,
                 http::handler_fn handler, cache_group_registry *registry)
    -> http::handler_fn {
  return [&store, opts = std::move(opts), handler = std::move(handler),
          registry](http::request_context &ctx) -> task<void> {
    if (opts.condition && !opts.condition(ctx)) {
      co_await handler(ctx);
      co_return;
    }
    co_await handler(ctx);
    if (opts.all_entries && registry && !opts.group.empty())
      co_await evict_all_in_group(store, *registry, opts.group);
    else {
      auto key = resolve_key(opts.key_pattern, ctx);
      co_await store.del(key);
      if (registry && !opts.group.empty())
        co_await registry->remove_key(opts.group, key);
    }
  };
}
auto cache_evict_group(cache_store &store, cache_group_registry &registry,
                       std::string group_name, http::handler_fn handler)
    -> http::handler_fn {
  return
      [&store, &registry, group_name = std::move(group_name),
       handler = std::move(handler)](http::request_context &ctx) -> task<void> {
        co_await handler(ctx);
        co_await evict_all_in_group(store, registry, group_name);
      };
}
auto make_cache_middleware(cache_store &store, global_cache_options opts,
                           cache_group_registry *registry)
    -> http::middleware_fn {
  return [&store, opts = std::move(opts), registry](
             http::request_context &ctx, http::next_fn next) -> task<void> {
    const bool should_cache =
        opts.cacheable_fn ? opts.cacheable_fn(ctx) : ctx.method() == "GET";
    if (!should_cache) {
      co_await next();
      co_return;
    }
    auto key = opts.key_prefix + std::string(ctx.path());
    const auto query = ctx.query_string();
    if (!query.empty()) {
      key += '?';
      key.append(query);
    }
    auto cached = co_await store.get(key);
    if (cached && set_cached_response(ctx, *cached))
      co_return;
    co_await next();
    co_await cache_response(store, registry, opts.group, key, opts.ttl, ctx);
    ctx.resp().set_header("X-Cache", "MISS");
  };
}
} // namespace cnetmod::cache
