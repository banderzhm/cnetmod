module cnetmod.protocol.mysql;
import :orm_performance;

namespace cnetmod::mysql::orm {
performance_interceptor::performance_interceptor(performance_config c)
    : config_(std::move(c)) {}

auto performance_interceptor::start_timing()
    -> std::chrono::steady_clock::time_point {
  return config_.enabled ? std::chrono::steady_clock::now()
                         : std::chrono::steady_clock::time_point{};
}

void performance_interceptor::end_timing(
    std::chrono::steady_clock::time_point s, std::string_view sql,
    std::uint64_t rows) {
  if (!config_.enabled)
    return;
  auto d = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - s);
  std::lock_guard lock(mutex_);
  history_.push_back({std::string(sql), d, std::chrono::system_clock::now(),
                      rows, d >= config_.slow_query_threshold});
  if (history_.size() > config_.max_history)
    history_.erase(history_.begin(),
                   history_.begin() + (history_.size() - config_.max_history));
  ++total_queries_;
  total_execution_time_ += d;
  if (history_.back().is_slow)
    ++slow_queries_;
}

auto performance_interceptor::get_slow_queries() const
    -> std::vector<sql_stat> {
  std::lock_guard lock(mutex_);
  std::vector<sql_stat> r;
  for (auto const &s : history_)
    if (s.is_slow)
      r.push_back(s);
  return r;
}

auto performance_interceptor::get_history() const -> std::vector<sql_stat> {
  std::lock_guard lock(mutex_);
  return history_;
}

auto performance_interceptor::get_summary() const
    -> std::tuple<std::uint64_t, std::uint64_t, std::chrono::microseconds> {
  std::lock_guard lock(mutex_);
  return {total_queries_, slow_queries_, total_execution_time_};
}

auto performance_interceptor::get_average_time() const
    -> std::chrono::microseconds {
  std::lock_guard lock(mutex_);
  return total_queries_ ? std::chrono::duration_cast<std::chrono::microseconds>(
                              total_execution_time_ / total_queries_)
                        : std::chrono::microseconds{};
}

void performance_interceptor::clear() {
  std::lock_guard lock(mutex_);
  history_.clear();
  total_queries_ = slow_queries_ = 0;
  total_execution_time_ = {};
}

auto performance_interceptor::config() const noexcept
    -> const performance_config & {
  return config_;
}
void performance_interceptor::set_config(performance_config c) {
  config_ = std::move(c);
}
void performance_interceptor::set_enabled(bool e) { config_.enabled = e; }

auto performance_interceptor::truncate_sql(std::string_view s, std::size_t n)
    -> std::string {
  return s.size() <= n ? std::string(s)
                       : std::format("{}...", s.substr(0, n - 3));
}

auto global_performance_interceptor() -> performance_interceptor & {
  static performance_interceptor x;
  return x;
}

performance_timer::performance_timer(std::string sql)
    : sql_(std::move(sql)),
      start_(global_performance_interceptor().start_timing()) {}

performance_timer::~performance_timer() {
  global_performance_interceptor().end_timing(start_, sql_, affected_rows_);
}

void performance_timer::set_affected_rows(std::uint64_t r) {
  affected_rows_ = r;
}
} // namespace cnetmod::mysql::orm
