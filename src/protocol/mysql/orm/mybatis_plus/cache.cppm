export module cnetmod.protocol.mysql:orm_cache;

import std;
import :types;

namespace cnetmod::mysql::orm {

// =============================================================================
// cache_key — Key for cache entries
// =============================================================================

export struct cache_key {
    std::string statement_id;
    std::string sql;
    std::vector<param_value> params;

    auto operator==(const cache_key& other) const -> bool {
        if (statement_id != other.statement_id) return false;
        if (sql != other.sql) return false;
        if (params.size() != other.params.size()) return false;

        for (std::size_t i = 0; i < params.size(); ++i) {
            if (!params_equal(params[i], other.params[i])) return false;
        }

        return true;
    }

private:
    static auto params_equal(const param_value& a, const param_value& b) -> bool {
        if (a.kind != b.kind) return false;

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
            return a.str_val == b.str_val;
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
};

} // namespace cnetmod::mysql::orm

// Hash function for cache_key
template <>
struct std::hash<cnetmod::mysql::orm::cache_key> {
    auto operator()(const cnetmod::mysql::orm::cache_key& key) const noexcept -> std::size_t {
        std::size_t h = std::hash<std::string>{}(key.statement_id);
        h ^= std::hash<std::string>{}(key.sql) + 0x9e3779b9 + (h << 6) + (h >> 2);

        for (auto& param : key.params) {
            std::size_t param_hash = 0;
            switch (param.kind) {
            case cnetmod::mysql::param_value::kind_t::int64_kind:
                param_hash = std::hash<std::int64_t>{}(param.int_val);
                break;
            case cnetmod::mysql::param_value::kind_t::uint64_kind:
                param_hash = std::hash<std::uint64_t>{}(param.uint_val);
                break;
            case cnetmod::mysql::param_value::kind_t::double_kind:
                param_hash = std::hash<double>{}(param.double_val);
                break;
            case cnetmod::mysql::param_value::kind_t::string_kind:
            case cnetmod::mysql::param_value::kind_t::blob_kind:
                param_hash = std::hash<std::string>{}(param.str_val);
                break;
            default:
                param_hash = 0;
            }
            h ^= param_hash + 0x9e3779b9 + (h << 6) + (h >> 2);
        }

        return h;
    }
};

namespace cnetmod::mysql::orm {

// =============================================================================
// cache_entry — Cached result set
// =============================================================================

export struct cache_entry {
    result_set data;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_accessed;
    std::size_t access_count = 0;

    auto is_expired(std::chrono::milliseconds ttl) const -> bool {
        auto now = std::chrono::steady_clock::now();
        return (now - created_at) > ttl;
    }
};

// =============================================================================
// cache_config — Cache configuration
// =============================================================================

export struct cache_config {
    bool enabled = true;
    std::size_t max_size = 1024;                           // Max cache entries
    std::chrono::milliseconds ttl{60000};                  // Time-to-live (60s)
    std::chrono::milliseconds eviction_interval{10000};    // Eviction check interval (10s)
    bool use_lru = true;                                   // Use LRU eviction policy
};

// =============================================================================
// query_cache — First-level cache (session-level)
// =============================================================================

export class query_cache {
public:
    explicit query_cache(cache_config config = {})
        : config_(std::move(config)) {}

    /// Get cached result
    auto get(const cache_key& key) -> std::optional<result_set> {
        if (!config_.enabled) return std::nullopt;

        std::lock_guard lock(mutex_);

        auto it = cache_.find(key);
        if (it == cache_.end()) return std::nullopt;

        auto& entry = it->second;

        // Check expiration
        if (entry.is_expired(config_.ttl)) {
            cache_.erase(it);
            return std::nullopt;
        }

        // Update access info
        entry.last_accessed = std::chrono::steady_clock::now();
        entry.access_count++;

        return entry.data;
    }

    /// Put result into cache
    void put(const cache_key& key, const result_set& data) {
        if (!config_.enabled) return;

        std::lock_guard lock(mutex_);

        // Check size limit
        if (cache_.size() >= config_.max_size) {
            evict_one();
        }

        cache_entry entry;
        entry.data = data;
        entry.created_at = std::chrono::steady_clock::now();
        entry.last_accessed = entry.created_at;
        entry.access_count = 0;

        cache_[key] = std::move(entry);
    }

    /// Clear all cache
    void clear() {
        std::lock_guard lock(mutex_);
        cache_.clear();
    }

    /// Clear cache for specific statement
    void clear_statement(std::string_view statement_id) {
        std::lock_guard lock(mutex_);

        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first.statement_id == statement_id) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Get cache statistics
    auto stats() const -> std::tuple<std::size_t, std::size_t, std::size_t> {
        std::lock_guard lock(mutex_);
        std::size_t total_access = 0;
        for (auto& [key, entry] : cache_) {
            total_access += entry.access_count;
        }
        return {cache_.size(), total_access, config_.max_size};
    }

    /// Enable/disable cache
    void set_enabled(bool enabled) { config_.enabled = enabled; }

    /// Get configuration
    auto config() const noexcept -> const cache_config& { return config_; }

private:
    cache_config config_;
    std::unordered_map<cache_key, cache_entry> cache_;
    mutable std::mutex mutex_;

    // Evict one entry using LRU or FIFO
    void evict_one() {
        if (cache_.empty()) return;

        if (config_.use_lru) {
            // Find least recently used
            auto lru_it = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.last_accessed < lru_it->second.last_accessed) {
                    lru_it = it;
                }
            }
            cache_.erase(lru_it);
        } else {
            // FIFO: remove oldest
            auto oldest_it = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.created_at < oldest_it->second.created_at) {
                    oldest_it = it;
                }
            }
            cache_.erase(oldest_it);
        }
    }
};

// =============================================================================
// second_level_cache — Second-level cache (application-level, shared)
// =============================================================================

export class second_level_cache {
public:
    explicit second_level_cache(cache_config config = {})
        : config_(std::move(config)) {
        // Start background eviction thread
        if (config_.enabled) {
            start_eviction_thread();
        }
    }

    ~second_level_cache() {
        stop_eviction_thread();
    }

    /// Get cached result
    auto get(const cache_key& key) -> std::optional<result_set> {
        if (!config_.enabled) return std::nullopt;

        std::shared_lock lock(mutex_);

        auto it = cache_.find(key);
        if (it == cache_.end()) return std::nullopt;

        auto& entry = it->second;

        // Check expiration
        if (entry.is_expired(config_.ttl)) {
            lock.unlock();
            std::unique_lock write_lock(mutex_);
            cache_.erase(it);
            return std::nullopt;
        }

        // Update access info (requires write lock)
        lock.unlock();
        std::unique_lock write_lock(mutex_);
        entry.last_accessed = std::chrono::steady_clock::now();
        entry.access_count++;

        return entry.data;
    }

    /// Put result into cache
    void put(const cache_key& key, const result_set& data) {
        if (!config_.enabled) return;

        std::unique_lock lock(mutex_);

        // Check size limit
        if (cache_.size() >= config_.max_size) {
            evict_one();
        }

        cache_entry entry;
        entry.data = data;
        entry.created_at = std::chrono::steady_clock::now();
        entry.last_accessed = entry.created_at;
        entry.access_count = 0;

        cache_[key] = std::move(entry);
    }

    /// Clear all cache
    void clear() {
        std::unique_lock lock(mutex_);
        cache_.clear();
    }

    /// Clear cache for specific statement
    void clear_statement(std::string_view statement_id) {
        std::unique_lock lock(mutex_);

        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first.statement_id == statement_id) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Enable/disable cache
    void set_enabled(bool enabled) {
        config_.enabled = enabled;
        if (enabled && !eviction_thread_.joinable()) {
            start_eviction_thread();
        } else if (!enabled && eviction_thread_.joinable()) {
            stop_eviction_thread();
        }
    }

private:
    cache_config config_;
    std::unordered_map<cache_key, cache_entry> cache_;
    mutable std::shared_mutex mutex_;

    std::jthread eviction_thread_;
    std::atomic<bool> stop_eviction_{false};

    void evict_one() {
        if (cache_.empty()) return;

        if (config_.use_lru) {
            auto lru_it = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.last_accessed < lru_it->second.last_accessed) {
                    lru_it = it;
                }
            }
            cache_.erase(lru_it);
        } else {
            auto oldest_it = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.created_at < oldest_it->second.created_at) {
                    oldest_it = it;
                }
            }
            cache_.erase(oldest_it);
        }
    }

    void start_eviction_thread() {
        stop_eviction_ = false;
        eviction_thread_ = std::jthread([this](std::stop_token stop_token) {
            while (!stop_token.stop_requested() && !stop_eviction_) {
                std::this_thread::sleep_for(config_.eviction_interval);
                evict_expired();
            }
        });
    }

    void stop_eviction_thread() {
        stop_eviction_ = true;
        if (eviction_thread_.joinable()) {
            eviction_thread_.request_stop();
            eviction_thread_.join();
        }
    }

    void evict_expired() {
        std::unique_lock lock(mutex_);

        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.is_expired(config_.ttl)) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// =============================================================================
// Global cache instances
// =============================================================================

export inline second_level_cache& global_second_level_cache() {
    static second_level_cache instance;
    return instance;
}

} // namespace cnetmod::mysql::orm
