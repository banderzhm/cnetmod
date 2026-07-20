/**
 * @file metrics.cppm
 * @brief Public HTTP Prometheus/OpenMetrics middleware declarations.
 */
export module cnetmod.protocol.http.middleware.metrics;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

export class metrics_collector {
public:
  metrics_collector() noexcept;

  std::atomic<std::uint64_t> requests_total{0};
  std::atomic<std::uint64_t> responses_2xx{0};
  std::atomic<std::uint64_t> responses_3xx{0};
  std::atomic<std::uint64_t> responses_4xx{0};
  std::atomic<std::uint64_t> responses_5xx{0};
  std::atomic<std::uint64_t> connections_total{0};
  std::atomic<std::int64_t> connections_active{0};
  std::atomic<std::uint64_t> latency_le_1ms{0};
  std::atomic<std::uint64_t> latency_le_5ms{0};
  std::atomic<std::uint64_t> latency_le_10ms{0};
  std::atomic<std::uint64_t> latency_le_50ms{0};
  std::atomic<std::uint64_t> latency_le_100ms{0};
  std::atomic<std::uint64_t> latency_le_500ms{0};
  std::atomic<std::uint64_t> latency_le_1s{0};
  std::atomic<std::uint64_t> latency_le_5s{0};
  std::atomic<std::uint64_t> latency_le_inf{0};
  std::atomic<std::uint64_t> latency_sum_us{0};
  std::atomic<std::uint64_t> response_bytes_total{0};

  void record_latency(std::uint64_t microseconds) noexcept;
  [[nodiscard]] auto uptime_seconds() const noexcept -> double;
  [[nodiscard]] auto serialize() const -> std::string;

private:
  std::chrono::steady_clock::time_point start_time_;
};

export auto metrics_middleware(metrics_collector &collector)
    -> http::middleware_fn;
export auto metrics_handler(metrics_collector &collector) -> http::handler_fn;

} // namespace cnetmod

namespace cnetmod::metrics {

export enum class metric_type { counter, gauge, histogram };
export using labels = std::map<std::string, std::string>;

struct metric_sample {
  labels sample_labels;
  double value = 0.0;
};

struct histogram_sample {
  labels sample_labels;
  std::vector<double> buckets;
  std::vector<std::uint64_t> counts;
  double sum = 0.0;
  std::uint64_t total = 0;
};

struct metric_family {
  metric_type type = metric_type::counter;
  std::string name;
  std::string help;
  std::map<std::string, metric_sample> samples;
  std::map<std::string, histogram_sample> histograms;
};

export class registry {
public:
  void counter_add(std::string_view name, double delta = 1.0,
                   labels metric_labels = {}, std::string_view help = {});
  void gauge_set(std::string_view name, double value, labels metric_labels = {},
                 std::string_view help = {});
  void histogram_observe(std::string_view name, double value,
                         std::vector<double> buckets, labels metric_labels = {},
                         std::string_view help = {});
  [[nodiscard]] auto render_openmetrics() const -> std::string;

private:
  mutable std::mutex mutex_;
  std::map<std::string, metric_family> families_;
};

export auto global_registry() -> registry &;
export auto openmetrics_handler(registry &metric_registry = global_registry())
    -> http::handler_fn;
export auto openmetrics_middleware(
    registry &metric_registry = global_registry(),
    std::vector<double> buckets = {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25,
                                   0.5, 1.0, 2.5, 5.0}) -> http::middleware_fn;

} // namespace cnetmod::metrics
