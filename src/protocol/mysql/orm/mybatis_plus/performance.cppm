export module cnetmod.protocol.mysql:orm_performance;
import std;
import :types;
import cnetmod.core.log;
export namespace cnetmod::mysql::orm {
struct sql_stat {
  std::string sql;
  std::chrono::microseconds execution_time;
  std::chrono::system_clock::time_point timestamp;
  std::uint64_t affected_rows{};
  bool is_slow{};
};

struct performance_config {
  bool enabled = true;
  std::chrono::microseconds slow_query_threshold{1000000};
  bool log_slow_queries = true;
  bool log_all_queries{};
  std::size_t max_history = 1000;
};

class performance_interceptor {
public:
  explicit performance_interceptor(performance_config = {});
  auto start_timing() -> std::chrono::steady_clock::time_point;
  void end_timing(std::chrono::steady_clock::time_point, std::string_view,
                  std::uint64_t = 0);
  auto get_slow_queries() const -> std::vector<sql_stat>;
  auto get_history() const -> std::vector<sql_stat>;
  auto get_summary() const
      -> std::tuple<std::uint64_t, std::uint64_t, std::chrono::microseconds>;
  auto get_average_time() const -> std::chrono::microseconds;
  void clear();
  auto config() const noexcept -> const performance_config &;
  void set_config(performance_config);
  void set_enabled(bool);

private:
  performance_config config_;
  std::vector<sql_stat> history_;
  std::uint64_t total_queries_{}, slow_queries_{};
  std::chrono::microseconds total_execution_time_{};
  mutable std::mutex mutex_;
  static auto truncate_sql(std::string_view, std::size_t) -> std::string;
};

auto global_performance_interceptor() -> performance_interceptor &;

class performance_timer {
public:
  explicit performance_timer(std::string);
  ~performance_timer();
  void set_affected_rows(std::uint64_t);
  performance_timer(const performance_timer &) = delete;
  performance_timer &operator=(const performance_timer &) = delete;

private:
  std::string sql_;
  std::chrono::steady_clock::time_point start_;
  std::uint64_t affected_rows_{};
};
} // namespace cnetmod::mysql::orm