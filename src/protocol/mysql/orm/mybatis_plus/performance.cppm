export module cnetmod.protocol.mysql:orm_performance;

import std;
import :types;
import cnetmod.core.log;

namespace cnetmod::mysql::orm {

// =============================================================================
// sql_stat — SQL execution statistics
// =============================================================================

export struct sql_stat {
    std::string sql;
    std::chrono::microseconds execution_time;
    std::chrono::system_clock::time_point timestamp;
    std::uint64_t affected_rows = 0;
    bool is_slow = false;
};

// =============================================================================
// performance_config — Performance analysis configuration
// =============================================================================

export struct performance_config {
    bool enabled = true;
    std::chrono::microseconds slow_query_threshold{1000000}; // 1 second
    bool log_slow_queries = true;
    bool log_all_queries = false;
    std::size_t max_history = 1000; // Max SQL history to keep
};

// =============================================================================
// performance_interceptor — Intercepts SQL execution for performance analysis
// =============================================================================

export class performance_interceptor {
public:
    explicit performance_interceptor(performance_config config = {})
        : config_(std::move(config)) {}

    /// Start timing for SQL execution
    auto start_timing() -> std::chrono::steady_clock::time_point {
        if (!config_.enabled) return {};
        return std::chrono::steady_clock::now();
    }

    /// End timing and record statistics
    void end_timing(std::chrono::steady_clock::time_point start,
                   std::string_view sql,
                   std::uint64_t affected_rows = 0) {
        if (!config_.enabled) return;

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        sql_stat stat;
        stat.sql = sql;
        stat.execution_time = duration;
        stat.timestamp = std::chrono::system_clock::now();
        stat.affected_rows = affected_rows;
        stat.is_slow = duration >= config_.slow_query_threshold;

        // Log if needed
        if (config_.log_all_queries) {
            logger::detail::write_log_no_src(logger::level::debug,
                std::format("[SQL] {} | {}μs | {} rows",
                    truncate_sql(sql, 100), duration.count(), affected_rows));
        } else if (stat.is_slow && config_.log_slow_queries) {
            logger::detail::write_log_no_src(logger::level::warn,
                std::format("[SLOW SQL] {} | {}μs | {} rows",
                    truncate_sql(sql, 200), duration.count(), affected_rows));
        }

        // Store in history
        std::lock_guard lock(mutex_);
        history_.push_back(std::move(stat));

        // Limit history size
        if (history_.size() > config_.max_history) {
            history_.erase(history_.begin(), history_.begin() + (history_.size() - config_.max_history));
        }

        // Update statistics
        total_queries_++;
        total_execution_time_ += duration;
        if (stat.is_slow) {
            slow_queries_++;
        }
    }

    /// Get slow queries
    auto get_slow_queries() const -> std::vector<sql_stat> {
        std::lock_guard lock(mutex_);
        std::vector<sql_stat> slow;
        for (auto& stat : history_) {
            if (stat.is_slow) {
                slow.push_back(stat);
            }
        }
        return slow;
    }

    /// Get all query history
    auto get_history() const -> std::vector<sql_stat> {
        std::lock_guard lock(mutex_);
        return history_;
    }

    /// Get statistics summary
    auto get_summary() const -> std::tuple<std::uint64_t, std::uint64_t, std::chrono::microseconds> {
        std::lock_guard lock(mutex_);
        return {total_queries_, slow_queries_, total_execution_time_};
    }

    /// Get average execution time
    auto get_average_time() const -> std::chrono::microseconds {
        std::lock_guard lock(mutex_);
        if (total_queries_ == 0) return {};
        return total_execution_time_ / total_queries_;
    }

    /// Clear history and statistics
    void clear() {
        std::lock_guard lock(mutex_);
        history_.clear();
        total_queries_ = 0;
        slow_queries_ = 0;
        total_execution_time_ = {};
    }

    /// Get configuration
    auto config() const noexcept -> const performance_config& { return config_; }

    /// Set configuration
    void set_config(performance_config config) { config_ = std::move(config); }

    /// Enable/disable performance analysis
    void set_enabled(bool enabled) { config_.enabled = enabled; }

private:
    performance_config config_;
    std::vector<sql_stat> history_;
    std::uint64_t total_queries_ = 0;
    std::uint64_t slow_queries_ = 0;
    std::chrono::microseconds total_execution_time_{};
    mutable std::mutex mutex_;

    static auto truncate_sql(std::string_view sql, std::size_t max_len) -> std::string {
        if (sql.size() <= max_len) return std::string(sql);
        return std::format("{}...", sql.substr(0, max_len - 3));
    }
};

// =============================================================================
// Global performance interceptor instance
// =============================================================================

export inline performance_interceptor& global_performance_interceptor() {
    static performance_interceptor instance;
    return instance;
}

// =============================================================================
// RAII performance timer
// =============================================================================

export class performance_timer {
public:
    explicit performance_timer(std::string sql)
        : sql_(std::move(sql)),
          start_(global_performance_interceptor().start_timing()) {}

    ~performance_timer() {
        global_performance_interceptor().end_timing(start_, sql_, affected_rows_);
    }

    void set_affected_rows(std::uint64_t rows) { affected_rows_ = rows; }

    performance_timer(const performance_timer&) = delete;
    performance_timer& operator=(const performance_timer&) = delete;

private:
    std::string sql_;
    std::chrono::steady_clock::time_point start_;
    std::uint64_t affected_rows_ = 0;
};

} // namespace cnetmod::mysql::orm
