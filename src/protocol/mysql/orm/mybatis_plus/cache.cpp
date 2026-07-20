module cnetmod.protocol.mysql;
import :orm_cache;

namespace cnetmod::mysql::orm {
auto cache_key::params_equal(const param_value &a, const param_value &b)
    -> bool {
  if (a.kind != b.kind)
    return false;
  switch (a.kind) {
  case param_value::kind_t::null_kind:
    return true;
  case param_value::kind_t::int64_kind:
    return a.int_val == b.int_val;
  case param_value::kind_t::uint64_kind:
    return a.uint_val == b.uint_val;
  case param_value::kind_t::double_kind:
    return a.double_val == b.double_val;
  case param_value::kind_t::string_kind:
  case param_value::kind_t::blob_kind:
    return a.str_val == b.str_val;
  case param_value::kind_t::date_kind:
    return a.date_val.year == b.date_val.year &&
           a.date_val.month == b.date_val.month &&
           a.date_val.day == b.date_val.day;
  case param_value::kind_t::datetime_kind:
    return a.datetime_val.year == b.datetime_val.year &&
           a.datetime_val.month == b.datetime_val.month &&
           a.datetime_val.day == b.datetime_val.day &&
           a.datetime_val.hour == b.datetime_val.hour &&
           a.datetime_val.minute == b.datetime_val.minute &&
           a.datetime_val.second == b.datetime_val.second;
  case param_value::kind_t::time_kind:
    return a.time_val.hours == b.time_val.hours &&
           a.time_val.minutes == b.time_val.minutes &&
           a.time_val.seconds == b.time_val.seconds;
  default:
    return false;
  }
}
auto cache_key::operator==(const cache_key &other) const -> bool {
  if (statement_id != other.statement_id || sql != other.sql ||
      params.size() != other.params.size())
    return false;
  for (std::size_t i = 0; i < params.size(); ++i)
    if (!params_equal(params[i], other.params[i]))
      return false;
  return true;
}
auto cache_entry::is_expired(std::chrono::milliseconds ttl) const -> bool {
  return std::chrono::steady_clock::now() - created_at > ttl;
}

query_cache::query_cache(cache_config config) : config_(std::move(config)) {}
auto query_cache::get(const cache_key &key) -> std::optional<result_set> {
  if (!config_.enabled)
    return std::nullopt;
  std::lock_guard lock(mutex_);
  const auto it = cache_.find(key);
  if (it == cache_.end())
    return std::nullopt;
  if (it->second.is_expired(config_.ttl)) {
    cache_.erase(it);
    return std::nullopt;
  }
  it->second.last_accessed = std::chrono::steady_clock::now();
  ++it->second.access_count;
  return it->second.data;
}
void query_cache::put(const cache_key &key, const result_set &data) {
  if (!config_.enabled)
    return;
  std::lock_guard lock(mutex_);
  if (cache_.size() >= config_.max_size)
    evict_one();
  cache_entry entry{data, std::chrono::steady_clock::now(), {}, 0};
  entry.last_accessed = entry.created_at;
  cache_[key] = std::move(entry);
}
void query_cache::clear() {
  std::lock_guard lock(mutex_);
  cache_.clear();
}
void query_cache::clear_statement(std::string_view statement_id) {
  std::lock_guard lock(mutex_);
  for (auto it = cache_.begin(); it != cache_.end();)
    it = it->first.statement_id == statement_id ? cache_.erase(it)
                                                : std::next(it);
}
auto query_cache::stats() const
    -> std::tuple<std::size_t, std::size_t, std::size_t> {
  std::lock_guard lock(mutex_);
  std::size_t accesses{};
  for (const auto &[_, entry] : cache_)
    accesses += entry.access_count;
  return {cache_.size(), accesses, config_.max_size};
}
void query_cache::set_enabled(bool enabled) { config_.enabled = enabled; }
auto query_cache::config() const noexcept -> const cache_config & {
  return config_;
}
void query_cache::evict_one() {
  if (cache_.empty())
    return;
  auto victim = cache_.begin();
  for (auto it = std::next(victim); it != cache_.end(); ++it) {
    const auto left =
        config_.use_lru ? it->second.last_accessed : it->second.created_at;
    const auto right = config_.use_lru ? victim->second.last_accessed
                                       : victim->second.created_at;
    if (left < right)
      victim = it;
  }
  cache_.erase(victim);
}

second_level_cache::second_level_cache(cache_config config)
    : config_(std::move(config)) {
  if (config_.enabled)
    start_eviction_thread();
}
second_level_cache::~second_level_cache() { stop_eviction_thread(); }
auto second_level_cache::get(const cache_key &key)
    -> std::optional<result_set> {
  if (!config_.enabled)
    return std::nullopt;
  std::shared_lock read_lock(mutex_);
  const auto it = cache_.find(key);
  if (it == cache_.end())
    return std::nullopt;
  if (it->second.is_expired(config_.ttl)) {
    read_lock.unlock();
    std::unique_lock write_lock(mutex_);
    cache_.erase(key);
    return std::nullopt;
  }
  read_lock.unlock();
  std::unique_lock write_lock(mutex_);
  const auto current = cache_.find(key);
  if (current == cache_.end())
    return std::nullopt;
  current->second.last_accessed = std::chrono::steady_clock::now();
  ++current->second.access_count;
  return current->second.data;
}
void second_level_cache::put(const cache_key &key, const result_set &data) {
  if (!config_.enabled)
    return;
  std::unique_lock lock(mutex_);
  if (cache_.size() >= config_.max_size)
    evict_one();
  cache_entry entry{data, std::chrono::steady_clock::now(), {}, 0};
  entry.last_accessed = entry.created_at;
  cache_[key] = std::move(entry);
}
void second_level_cache::clear() {
  std::unique_lock lock(mutex_);
  cache_.clear();
}
void second_level_cache::clear_statement(std::string_view statement_id) {
  std::unique_lock lock(mutex_);
  for (auto it = cache_.begin(); it != cache_.end();)
    it = it->first.statement_id == statement_id ? cache_.erase(it)
                                                : std::next(it);
}
void second_level_cache::set_enabled(bool enabled) {
  config_.enabled = enabled;
  if (enabled && !eviction_thread_.joinable())
    start_eviction_thread();
  else if (!enabled && eviction_thread_.joinable())
    stop_eviction_thread();
}
void second_level_cache::evict_one() {
  if (cache_.empty())
    return;
  auto victim = cache_.begin();
  for (auto it = std::next(victim); it != cache_.end(); ++it) {
    const auto left =
        config_.use_lru ? it->second.last_accessed : it->second.created_at;
    const auto right = config_.use_lru ? victim->second.last_accessed
                                       : victim->second.created_at;
    if (left < right)
      victim = it;
  }
  cache_.erase(victim);
}
void second_level_cache::start_eviction_thread() {
  stop_eviction_ = false;
  eviction_thread_ = std::jthread([this](std::stop_token stop) {
    while (!stop.stop_requested() && !stop_eviction_) {
      std::this_thread::sleep_for(config_.eviction_interval);
      evict_expired();
    }
  });
}
void second_level_cache::stop_eviction_thread() {
  stop_eviction_ = true;
  if (eviction_thread_.joinable()) {
    eviction_thread_.request_stop();
    eviction_thread_.join();
  }
}
void second_level_cache::evict_expired() {
  std::unique_lock lock(mutex_);
  for (auto it = cache_.begin(); it != cache_.end();)
    it = it->second.is_expired(config_.ttl) ? cache_.erase(it) : std::next(it);
}
auto global_second_level_cache() -> second_level_cache & {
  static second_level_cache instance;
  return instance;
}
} // namespace cnetmod::mysql::orm

auto std::hash<cnetmod::mysql::orm::cache_key>::operator()(
    const cnetmod::mysql::orm::cache_key &key) const noexcept -> std::size_t {
  std::size_t hash = std::hash<std::string>{}(key.statement_id);
  hash ^= std::hash<std::string>{}(key.sql) + 0x9e3779b9 + (hash << 6) +
          (hash >> 2);
  for (const auto &param : key.params) {
    std::size_t value{};
    switch (param.kind) {
    case cnetmod::mysql::param_value::kind_t::int64_kind:
      value = std::hash<std::int64_t>{}(param.int_val);
      break;
    case cnetmod::mysql::param_value::kind_t::uint64_kind:
      value = std::hash<std::uint64_t>{}(param.uint_val);
      break;
    case cnetmod::mysql::param_value::kind_t::double_kind:
      value = std::hash<double>{}(param.double_val);
      break;
    case cnetmod::mysql::param_value::kind_t::string_kind:
    case cnetmod::mysql::param_value::kind_t::blob_kind:
      value = std::hash<std::string>{}(param.str_val);
      break;
    default:
      break;
    }
    hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return hash;
}
