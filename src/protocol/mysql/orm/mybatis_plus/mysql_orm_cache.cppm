export module cnetmod.protocol.mysql:orm_cache;
// Protocol-qualified filename prevents duplicate source basenames.

import std;
import :types;

namespace cnetmod::mysql::orm {

export struct cache_key {
  std::string statement_id;
  std::string sql;
  std::vector<param_value> params;
  auto operator==(const cache_key &other) const -> bool;

private:
  static auto params_equal(const param_value &a, const param_value &b) -> bool;
};

} // namespace cnetmod::mysql::orm

template <> struct std::hash<cnetmod::mysql::orm::cache_key> {
  auto operator()(const cnetmod::mysql::orm::cache_key &key) const noexcept
      -> std::size_t;
};

namespace cnetmod::mysql::orm {

export struct cache_entry {
  result_set data;
  std::chrono::steady_clock::time_point created_at;
  std::chrono::steady_clock::time_point last_accessed;
  std::size_t access_count = 0;
  auto is_expired(std::chrono::milliseconds ttl) const -> bool;
};

export struct cache_config {
  bool enabled = true;
  std::size_t max_size = 1024;
  std::chrono::milliseconds ttl{60000};
  std::chrono::milliseconds eviction_interval{10000};
  bool use_lru = true;
};

export class query_cache {
public:
  explicit query_cache(cache_config config = {});
  auto get(const cache_key &key) -> std::optional<result_set>;
  void put(const cache_key &key, const result_set &data);
  void clear();
  void clear_statement(std::string_view statement_id);
  auto stats() const -> std::tuple<std::size_t, std::size_t, std::size_t>;
  void set_enabled(bool enabled);
  auto config() const noexcept -> const cache_config &;

private:
  cache_config config_;
  std::unordered_map<cache_key, cache_entry> cache_;
  mutable std::mutex mutex_;
  void evict_one();
};

export class second_level_cache {
public:
  explicit second_level_cache(cache_config config = {});
  ~second_level_cache();
  second_level_cache(const second_level_cache &) = delete;
  auto operator=(const second_level_cache &) -> second_level_cache & = delete;
  auto get(const cache_key &key) -> std::optional<result_set>;
  void put(const cache_key &key, const result_set &data);
  void clear();
  void clear_statement(std::string_view statement_id);
  void set_enabled(bool enabled);

private:
  cache_config config_;
  std::unordered_map<cache_key, cache_entry> cache_;
  mutable std::shared_mutex mutex_;
  std::jthread eviction_thread_;
  std::atomic<bool> stop_eviction_{false};
  void evict_one();
  void start_eviction_thread();
  void stop_eviction_thread();
  void evict_expired();
};

export auto global_second_level_cache() -> second_level_cache &;
} // namespace cnetmod::mysql::orm
