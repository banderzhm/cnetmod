module cnetmod.protocol.http.middleware.metrics;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

metrics_collector::metrics_collector() noexcept
    : start_time_(std::chrono::steady_clock::now()) {}

void metrics_collector::record_latency(std::uint64_t us) noexcept {
  latency_sum_us.fetch_add(us, std::memory_order_relaxed);
  latency_le_inf.fetch_add(1, std::memory_order_relaxed);
  if (us <= 5'000'000)
    latency_le_5s.fetch_add(1, std::memory_order_relaxed);
  if (us <= 1'000'000)
    latency_le_1s.fetch_add(1, std::memory_order_relaxed);
  if (us <= 500'000)
    latency_le_500ms.fetch_add(1, std::memory_order_relaxed);
  if (us <= 100'000)
    latency_le_100ms.fetch_add(1, std::memory_order_relaxed);
  if (us <= 50'000)
    latency_le_50ms.fetch_add(1, std::memory_order_relaxed);
  if (us <= 10'000)
    latency_le_10ms.fetch_add(1, std::memory_order_relaxed);
  if (us <= 5'000)
    latency_le_5ms.fetch_add(1, std::memory_order_relaxed);
  if (us <= 1'000)
    latency_le_1ms.fetch_add(1, std::memory_order_relaxed);
}

auto metrics_collector::uptime_seconds() const noexcept -> double {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                       start_time_)
      .count();
}

auto metrics_collector::serialize() const -> std::string {
  const auto total = requests_total.load(std::memory_order_relaxed);
  const auto sum_seconds =
      static_cast<double>(latency_sum_us.load(std::memory_order_relaxed)) /
      1'000'000.0;
  std::string out;
  out.reserve(2048);
  out += std::format(
      "# HELP cnetmod_uptime_seconds Process uptime.\n# TYPE "
      "cnetmod_uptime_seconds gauge\ncnetmod_uptime_seconds {:.3f}\n",
      uptime_seconds());
  out += std::format(
      "# HELP cnetmod_http_requests_total Total HTTP requests.\n# TYPE "
      "cnetmod_http_requests_total counter\ncnetmod_http_requests_total {}\n",
      total);
  out += std::format("# HELP cnetmod_http_responses_total Responses by status "
                     "class.\n# TYPE cnetmod_http_responses_total "
                     "counter\ncnetmod_http_responses_total{{code=\"2xx\"}} "
                     "{}\ncnetmod_http_responses_total{{code=\"3xx\"}} "
                     "{}\ncnetmod_http_responses_total{{code=\"4xx\"}} "
                     "{}\ncnetmod_http_responses_total{{code=\"5xx\"}} {}\n",
                     responses_2xx.load(std::memory_order_relaxed),
                     responses_3xx.load(std::memory_order_relaxed),
                     responses_4xx.load(std::memory_order_relaxed),
                     responses_5xx.load(std::memory_order_relaxed));
  out += std::format(
      "# HELP cnetmod_connections_total Total accepted connections.\n# TYPE "
      "cnetmod_connections_total counter\ncnetmod_connections_total {}\n",
      connections_total.load(std::memory_order_relaxed));
  out += std::format(
      "# HELP cnetmod_connections_active Current active connections.\n# TYPE "
      "cnetmod_connections_active gauge\ncnetmod_connections_active {}\n",
      connections_active.load(std::memory_order_relaxed));
  out += std::format(
      "# HELP cnetmod_http_request_duration_seconds Request latency "
      "histogram.\n# TYPE cnetmod_http_request_duration_seconds "
      "histogram\ncnetmod_http_request_duration_seconds_bucket{{le=\"0.001\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"0.005\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"0.01\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"0.05\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"0.1\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"0.5\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"1\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"5\"}} "
      "{}\ncnetmod_http_request_duration_seconds_bucket{{le=\"+Inf\"}} "
      "{}\ncnetmod_http_request_duration_seconds_sum "
      "{:.6f}\ncnetmod_http_request_duration_seconds_count {}\n",
      latency_le_1ms.load(std::memory_order_relaxed),
      latency_le_5ms.load(std::memory_order_relaxed),
      latency_le_10ms.load(std::memory_order_relaxed),
      latency_le_50ms.load(std::memory_order_relaxed),
      latency_le_100ms.load(std::memory_order_relaxed),
      latency_le_500ms.load(std::memory_order_relaxed),
      latency_le_1s.load(std::memory_order_relaxed),
      latency_le_5s.load(std::memory_order_relaxed),
      latency_le_inf.load(std::memory_order_relaxed), sum_seconds, total);
  out += std::format("# HELP cnetmod_http_response_bytes_total Total response "
                     "body bytes.\n# TYPE cnetmod_http_response_bytes_total "
                     "counter\ncnetmod_http_response_bytes_total {}\n",
                     response_bytes_total.load(std::memory_order_relaxed));
  return out;
}

auto metrics_middleware(metrics_collector &collector) -> http::middleware_fn {
  return [&collector](http::request_context &context,
                      http::next_fn next) -> task<void> {
    collector.requests_total.fetch_add(1, std::memory_order_relaxed);
    const auto started = std::chrono::steady_clock::now();
    co_await next();
    collector.record_latency(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count()));
    const auto status = context.resp().status_code();
    if (status >= 200 && status < 300)
      collector.responses_2xx.fetch_add(1, std::memory_order_relaxed);
    else if (status >= 300 && status < 400)
      collector.responses_3xx.fetch_add(1, std::memory_order_relaxed);
    else if (status >= 400 && status < 500)
      collector.responses_4xx.fetch_add(1, std::memory_order_relaxed);
    else if (status >= 500)
      collector.responses_5xx.fetch_add(1, std::memory_order_relaxed);
    collector.response_bytes_total.fetch_add(context.resp().body().size(),
                                             std::memory_order_relaxed);
  };
}

auto metrics_handler(metrics_collector &collector) -> http::handler_fn {
  return [&collector](http::request_context &context) -> task<void> {
    context.resp().set_status(http::status::ok);
    context.resp().set_header("Content-Type",
                              "text/plain; version=0.0.4; charset=utf-8");
    context.resp().set_body(collector.serialize());
    co_return;
  };
}

} // namespace cnetmod

namespace cnetmod::metrics {
namespace {
auto escape_label(std::string_view value) -> std::string {
  std::string out;
  out.reserve(value.size());
  for (const auto ch : value) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(ch);
    }
  }
  return out;
}
auto labels_key(const labels &metric_labels) -> std::string {
  std::string key;
  for (const auto &[name, value] : metric_labels) {
    key += name;
    key += '=';
    key += value;
    key.push_back('\xff');
  }
  return key;
}
auto labels_text(const labels &metric_labels) -> std::string {
  if (metric_labels.empty())
    return {};
  std::string out{"{"};
  for (auto it = metric_labels.begin(); it != metric_labels.end(); ++it) {
    if (it != metric_labels.begin())
      out += ',';
    out += it->first;
    out += "=\"";
    out += escape_label(it->second);
    out += '"';
  }
  return out + '}';
}
} // namespace

void registry::counter_add(std::string_view name, double delta,
                           labels metric_labels, std::string_view help) {
  std::scoped_lock lock(mutex_);
  auto &family = families_[std::string(name)];
  family.type = metric_type::counter;
  family.name = name;
  if (!help.empty())
    family.help = help;
  auto &sample = family.samples[labels_key(metric_labels)];
  sample.sample_labels = std::move(metric_labels);
  sample.value += delta;
}
void registry::gauge_set(std::string_view name, double value,
                         labels metric_labels, std::string_view help) {
  std::scoped_lock lock(mutex_);
  auto &family = families_[std::string(name)];
  family.type = metric_type::gauge;
  family.name = name;
  if (!help.empty())
    family.help = help;
  auto &sample = family.samples[labels_key(metric_labels)];
  sample.sample_labels = std::move(metric_labels);
  sample.value = value;
}
void registry::histogram_observe(std::string_view name, double value,
                                 std::vector<double> buckets,
                                 labels metric_labels, std::string_view help) {
  std::ranges::sort(buckets);
  std::scoped_lock lock(mutex_);
  auto &family = families_[std::string(name)];
  family.type = metric_type::histogram;
  family.name = name;
  if (!help.empty())
    family.help = help;
  auto &histogram = family.histograms[labels_key(metric_labels)];
  histogram.sample_labels = std::move(metric_labels);
  if (histogram.buckets.empty()) {
    histogram.buckets = std::move(buckets);
    histogram.counts.assign(histogram.buckets.size(), 0);
  }
  for (std::size_t i = 0; i < histogram.buckets.size(); ++i)
    if (value <= histogram.buckets[i])
      ++histogram.counts[i];
  histogram.sum += value;
  ++histogram.total;
}
auto registry::render_openmetrics() const -> std::string {
  std::scoped_lock lock(mutex_);
  std::string out;
  for (const auto &[_, family] : families_) {
    if (!family.help.empty())
      out += std::format("# HELP {} {}\n", family.name, family.help);
    out += std::format("# TYPE {} {}\n", family.name,
                       family.type == metric_type::counter ? "counter"
                       : family.type == metric_type::gauge ? "gauge"
                                                           : "histogram");
    if (family.type == metric_type::histogram)
      for (const auto &[__, histogram] : family.histograms) {
        for (std::size_t i = 0; i < histogram.buckets.size(); ++i) {
          auto metric_labels = histogram.sample_labels;
          metric_labels["le"] = std::format("{}", histogram.buckets[i]);
          out += std::format("{}_bucket{} {}\n", family.name,
                             labels_text(metric_labels), histogram.counts[i]);
        }
        auto metric_labels = histogram.sample_labels;
        metric_labels["le"] = "+Inf";
        out += std::format(
            "{}_bucket{} {}\n{}_sum{} {}\n{}_count{} {}\n", family.name,
            labels_text(metric_labels), histogram.total, family.name,
            labels_text(histogram.sample_labels), histogram.sum, family.name,
            labels_text(histogram.sample_labels), histogram.total);
      }
    else
      for (const auto &[__, sample] : family.samples)
        out += std::format("{}{} {}\n", family.name,
                           labels_text(sample.sample_labels), sample.value);
  }
  return out + "# EOF\n";
}
auto global_registry() -> registry & {
  static registry instance;
  return instance;
}
auto openmetrics_handler(registry &metric_registry) -> http::handler_fn {
  return [&metric_registry](http::request_context &context) -> task<void> {
    context.resp().set_status(http::status::ok);
    context.resp().set_header(
        "Content-Type",
        "application/openmetrics-text; version=1.0.0; charset=utf-8");
    context.resp().set_body(metric_registry.render_openmetrics());
    co_return;
  };
}
auto openmetrics_middleware(registry &metric_registry,
                            std::vector<double> buckets)
    -> http::middleware_fn {
  return [&metric_registry, buckets = std::move(buckets)](
             http::request_context &context, http::next_fn next) -> task<void> {
    const auto started = std::chrono::steady_clock::now();
    co_await next();
    labels metric_labels{
        {"method", std::string(context.method())},
        {"path", std::string(context.path())},
        {"status", std::to_string(context.resp().status_code())}};
    metric_registry.counter_add("http_requests_total", 1.0, metric_labels,
                                "Total HTTP requests.");
    metric_registry.histogram_observe(
        "http_request_duration_seconds",
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      started)
            .count(),
        buckets, std::move(metric_labels), "HTTP request duration in seconds.");
    co_return;
  };
}

} // namespace cnetmod::metrics
