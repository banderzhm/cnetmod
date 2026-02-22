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
 *   import cnetmod.middleware.metrics;
 *
 *   cnetmod::metrics_collector mc;
 *   svr.use(metrics_middleware(mc));
 *   router.get("/metrics", metrics_handler(mc));
 */
export module cnetmod.middleware.metrics;

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
