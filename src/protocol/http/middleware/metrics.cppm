/**
 * @file metrics.cppm
 * @brief Prometheus text format metrics collection — middleware auto-stats + /metrics endpoint exposure
 *
 * Three components:
 *   1. metrics_collector — Global metrics collector (atomic counters, zero overhead)
 *   2. metrics_middleware — Middleware, automatically tracks each request
 *   3. metrics_handler — Handler, exposes Prometheus text format
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.metrics;
 *
 *   cnetmod::metrics_collector mc;
 *   svr.use(metrics_middleware(mc));
 *   router.get("/metrics", metrics_handler(mc));
 */
export module cnetmod.protocol.http.middleware.metrics;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// metrics_collector — Prometheus metrics collector
// =============================================================================

export class metrics_collector {
public:
    metrics_collector() noexcept
        : start_time_(std::chrono::steady_clock::now()) {}

    // --- Request counters ---
    std::atomic<std::uint64_t> requests_total{0};
    std::atomic<std::uint64_t> responses_2xx{0};
    std::atomic<std::uint64_t> responses_3xx{0};
    std::atomic<std::uint64_t> responses_4xx{0};
    std::atomic<std::uint64_t> responses_5xx{0};

    // --- Connections ---
    std::atomic<std::uint64_t> connections_total{0};
    std::atomic<std::int64_t>  connections_active{0};

    // --- Latency histogram buckets (cumulative, Prometheus style) ---
    std::atomic<std::uint64_t> latency_le_1ms{0};     // ≤ 1ms
    std::atomic<std::uint64_t> latency_le_5ms{0};     // ≤ 5ms
    std::atomic<std::uint64_t> latency_le_10ms{0};    // ≤ 10ms
    std::atomic<std::uint64_t> latency_le_50ms{0};    // ≤ 50ms
    std::atomic<std::uint64_t> latency_le_100ms{0};   // ≤ 100ms
    std::atomic<std::uint64_t> latency_le_500ms{0};   // ≤ 500ms
    std::atomic<std::uint64_t> latency_le_1s{0};      // ≤ 1s
    std::atomic<std::uint64_t> latency_le_5s{0};      // ≤ 5s
    std::atomic<std::uint64_t> latency_le_inf{0};     // +Inf

    // --- Latency accumulation ---
    std::atomic<std::uint64_t> latency_sum_us{0};     // Total microseconds

    // --- Body size ---
    std::atomic<std::uint64_t> response_bytes_total{0};

    /// Record request latency (in microseconds)
    void record_latency(std::uint64_t us) noexcept {
        latency_sum_us.fetch_add(us, std::memory_order_relaxed);
        latency_le_inf.fetch_add(1, std::memory_order_relaxed);
        if (us <= 5000000) latency_le_5s.fetch_add(1, std::memory_order_relaxed);
        if (us <= 1000000) latency_le_1s.fetch_add(1, std::memory_order_relaxed);
        if (us <= 500000)  latency_le_500ms.fetch_add(1, std::memory_order_relaxed);
        if (us <= 100000)  latency_le_100ms.fetch_add(1, std::memory_order_relaxed);
        if (us <= 50000)   latency_le_50ms.fetch_add(1, std::memory_order_relaxed);
        if (us <= 10000)   latency_le_10ms.fetch_add(1, std::memory_order_relaxed);
        if (us <= 5000)    latency_le_5ms.fetch_add(1, std::memory_order_relaxed);
        if (us <= 1000)    latency_le_1ms.fetch_add(1, std::memory_order_relaxed);
    }

    /// Service uptime
    [[nodiscard]] auto uptime_seconds() const noexcept -> double {
        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        return std::chrono::duration<double>(elapsed).count();
    }

    /// Serialize to Prometheus text exposition format
    [[nodiscard]] auto serialize() const -> std::string {
        auto total = requests_total.load(std::memory_order_relaxed);
        auto sum_us = latency_sum_us.load(std::memory_order_relaxed);
        double sum_s = static_cast<double>(sum_us) / 1'000'000.0;

        std::string out;
        out.reserve(2048);

        // Process uptime
        out += std::format(
            "# HELP cnetmod_uptime_seconds Process uptime.\n"
            "# TYPE cnetmod_uptime_seconds gauge\n"
            "cnetmod_uptime_seconds {:.3f}\n", uptime_seconds());

        // Total requests
        out += std::format(
            "# HELP cnetmod_http_requests_total Total HTTP requests.\n"
            "# TYPE cnetmod_http_requests_total counter\n"
            "cnetmod_http_requests_total {}\n", total);

        // Responses by status code
        out += std::format(
            "# HELP cnetmod_http_responses_total Responses by status class.\n"
            "# TYPE cnetmod_http_responses_total counter\n"
            "cnetmod_http_responses_total{{code=\"2xx\"}} {}\n"
            "cnetmod_http_responses_total{{code=\"3xx\"}} {}\n"
            "cnetmod_http_responses_total{{code=\"4xx\"}} {}\n"
            "cnetmod_http_responses_total{{code=\"5xx\"}} {}\n",
            responses_2xx.load(std::memory_order_relaxed),
            responses_3xx.load(std::memory_order_relaxed),
            responses_4xx.load(std::memory_order_relaxed),
            responses_5xx.load(std::memory_order_relaxed));

        // Connections
        out += std::format(
            "# HELP cnetmod_connections_active Current active connections.\n"
            "# TYPE cnetmod_connections_active gauge\n"
            "cnetmod_connections_active {}\n",
            connections_active.load(std::memory_order_relaxed));

        // Latency histogram
        out += std::format(
            "# HELP cnetmod_http_request_duration_seconds Request latency histogram.\n"
            "# TYPE cnetmod_http_request_duration_seconds histogram\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"0.001\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"0.005\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"0.01\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"0.05\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"0.1\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"0.5\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"1\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"5\"}} {}\n"
            "cnetmod_http_request_duration_seconds_bucket{{le=\"+Inf\"}} {}\n"
            "cnetmod_http_request_duration_seconds_sum {:.6f}\n"
            "cnetmod_http_request_duration_seconds_count {}\n",
            latency_le_1ms.load(std::memory_order_relaxed),
            latency_le_5ms.load(std::memory_order_relaxed),
            latency_le_10ms.load(std::memory_order_relaxed),
            latency_le_50ms.load(std::memory_order_relaxed),
            latency_le_100ms.load(std::memory_order_relaxed),
            latency_le_500ms.load(std::memory_order_relaxed),
            latency_le_1s.load(std::memory_order_relaxed),
            latency_le_5s.load(std::memory_order_relaxed),
            latency_le_inf.load(std::memory_order_relaxed),
            sum_s, total);

        // Total response body bytes
        out += std::format(
            "# HELP cnetmod_http_response_bytes_total Total response body bytes.\n"
            "# TYPE cnetmod_http_response_bytes_total counter\n"
            "cnetmod_http_response_bytes_total {}\n",
            response_bytes_total.load(std::memory_order_relaxed));

        return out;
    }

private:
    std::chrono::steady_clock::time_point start_time_;
};

// =============================================================================
// metrics_middleware — Auto-tracking middleware
// =============================================================================

export inline auto metrics_middleware(metrics_collector& mc)
    -> http::middleware_fn
{
    return [&mc](http::request_context& ctx, http::next_fn next) -> task<void> {
        mc.requests_total.fetch_add(1, std::memory_order_relaxed);

        auto start = std::chrono::steady_clock::now();

        co_await next();

        // Latency
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
        mc.record_latency(us);

        // Status code classification
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300)
            mc.responses_2xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 300 && status < 400)
            mc.responses_3xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 400 && status < 500)
            mc.responses_4xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 500)
            mc.responses_5xx.fetch_add(1, std::memory_order_relaxed);

        // Body size
        mc.response_bytes_total.fetch_add(
            ctx.resp().body().size(), std::memory_order_relaxed);
    };
}

// =============================================================================
// metrics_handler — Expose /metrics endpoint
// =============================================================================

export inline auto metrics_handler(metrics_collector& mc) -> http::handler_fn
{
    return [&mc](http::request_context& ctx) -> task<void> {
        auto body = mc.serialize();
        ctx.resp().set_status(http::status::ok);
        ctx.resp().set_header("Content-Type",
            "text/plain; version=0.0.4; charset=utf-8");
        ctx.resp().set_body(std::move(body));
        co_return;
    };
}

} // namespace cnetmod

namespace cnetmod::metrics {

export enum class metric_type {
    counter,
    gauge,
    histogram,
};

export using labels = std::map<std::string, std::string>;

namespace detail {

inline auto escape_label(std::string_view value) -> std::string {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

inline auto labels_key(const labels& ls) -> std::string {
    std::string key;
    for (const auto& [k, v] : ls) {
        key += k;
        key.push_back('=');
        key += v;
        key.push_back('\xff');
    }
    return key;
}

inline auto labels_text(const labels& ls) -> std::string {
    if (ls.empty()) return {};
    std::string out = "{";
    bool first = true;
    for (const auto& [k, v] : ls) {
        if (!first) out += ",";
        first = false;
        out += k;
        out += "=\"";
        out += escape_label(v);
        out += "\"";
    }
    out += "}";
    return out;
}

} // namespace detail

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
                     labels ls = {}, std::string_view help = {}) {
        std::scoped_lock lock(mutex_);
        auto& family = families_[std::string(name)];
        family.type = metric_type::counter;
        family.name = std::string(name);
        if (!help.empty()) family.help = std::string(help);
        auto key = detail::labels_key(ls);
        auto& sample = family.samples[key];
        sample.sample_labels = std::move(ls);
        sample.value += delta;
    }

    void gauge_set(std::string_view name, double value,
                   labels ls = {}, std::string_view help = {}) {
        std::scoped_lock lock(mutex_);
        auto& family = families_[std::string(name)];
        family.type = metric_type::gauge;
        family.name = std::string(name);
        if (!help.empty()) family.help = std::string(help);
        auto key = detail::labels_key(ls);
        auto& sample = family.samples[key];
        sample.sample_labels = std::move(ls);
        sample.value = value;
    }

    void histogram_observe(std::string_view name, double value,
                           std::vector<double> buckets,
                           labels ls = {}, std::string_view help = {}) {
        std::ranges::sort(buckets);
        std::scoped_lock lock(mutex_);
        auto& family = families_[std::string(name)];
        family.type = metric_type::histogram;
        family.name = std::string(name);
        if (!help.empty()) family.help = std::string(help);
        auto key = detail::labels_key(ls);
        auto& h = family.histograms[key];
        h.sample_labels = std::move(ls);
        if (h.buckets.empty()) {
            h.buckets = std::move(buckets);
            h.counts.assign(h.buckets.size(), 0);
        }
        for (std::size_t i = 0; i < h.buckets.size(); ++i) {
            if (value <= h.buckets[i]) ++h.counts[i];
        }
        h.sum += value;
        ++h.total;
    }

    [[nodiscard]] auto render_openmetrics() const -> std::string {
        std::scoped_lock lock(mutex_);
        std::string out;
        for (const auto& [_, family] : families_) {
            if (!family.help.empty()) {
                out += "# HELP ";
                out += family.name;
                out.push_back(' ');
                out += family.help;
                out.push_back('\n');
            }
            out += "# TYPE ";
            out += family.name;
            out.push_back(' ');
            switch (family.type) {
            case metric_type::counter: out += "counter"; break;
            case metric_type::gauge: out += "gauge"; break;
            case metric_type::histogram: out += "histogram"; break;
            }
            out.push_back('\n');

            if (family.type == metric_type::histogram) {
                for (const auto& [__, h] : family.histograms) {
                    for (std::size_t i = 0; i < h.buckets.size(); ++i) {
                        auto ls = h.sample_labels;
                        ls["le"] = std::format("{}", h.buckets[i]);
                        out += family.name + "_bucket" + detail::labels_text(ls);
                        out += " " + std::to_string(h.counts[i]) + "\n";
                    }
                    auto ls = h.sample_labels;
                    ls["le"] = "+Inf";
                    out += family.name + "_bucket" + detail::labels_text(ls);
                    out += " " + std::to_string(h.total) + "\n";
                    out += family.name + "_sum" + detail::labels_text(h.sample_labels);
                    out += " " + std::format("{}", h.sum) + "\n";
                    out += family.name + "_count" + detail::labels_text(h.sample_labels);
                    out += " " + std::to_string(h.total) + "\n";
                }
            } else {
                for (const auto& [__, sample] : family.samples) {
                    out += family.name;
                    out += detail::labels_text(sample.sample_labels);
                    out += " ";
                    out += std::format("{}", sample.value);
                    out += "\n";
                }
            }
        }
        out += "# EOF\n";
        return out;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, metric_family> families_;
};

export inline auto global_registry() -> registry& {
    static registry r;
    return r;
}

export inline auto openmetrics_handler(registry& reg = global_registry())
    -> http::handler_fn
{
    return [&reg](http::request_context& ctx) -> task<void> {
        auto body = reg.render_openmetrics();
        ctx.resp().set_status(http::status::ok);
        ctx.resp().set_header("Content-Type", "application/openmetrics-text; version=1.0.0; charset=utf-8");
        ctx.resp().set_body(std::move(body));
        co_return;
    };
}

export inline auto openmetrics_middleware(registry& reg = global_registry(),
                                          std::vector<double> buckets = {
                                              0.001, 0.005, 0.01, 0.025, 0.05,
                                              0.1, 0.25, 0.5, 1.0, 2.5, 5.0})
    -> http::middleware_fn
{
    return [&reg, buckets = std::move(buckets)](
               http::request_context& ctx, http::next_fn next) -> task<void> {
        const auto started = std::chrono::steady_clock::now();
        co_await next();
        const auto seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();

        labels ls{
            {"method", std::string(ctx.method())},
            {"path", std::string(ctx.path())},
            {"status", std::to_string(ctx.resp().status_code())},
        };
        reg.counter_add("http_requests_total", 1.0, ls, "Total HTTP requests.");
        reg.histogram_observe("http_request_duration_seconds", seconds, buckets, std::move(ls),
            "HTTP request duration in seconds.");
        co_return;
    };
}

} // namespace cnetmod::metrics
