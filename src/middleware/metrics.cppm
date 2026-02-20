/**
 * @file metrics.cppm
 * @brief Prometheus 文本格式指标收集 — 中间件自动统计 + /metrics 端点暴露
 *
 * 三个组件:
 *   1. metrics_collector — 全局指标收集器（atomic 计数器，零开销）
 *   2. metrics_middleware — 中间件，自动统计每个请求
 *   3. metrics_handler — handler，暴露 Prometheus text format
 *
 * 使用示例:
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
// metrics_collector — Prometheus 指标收集器
// =============================================================================

export class metrics_collector {
public:
    metrics_collector() noexcept
        : start_time_(std::chrono::steady_clock::now()) {}

    // --- 请求计数 ---
    std::atomic<std::uint64_t> requests_total{0};
    std::atomic<std::uint64_t> responses_2xx{0};
    std::atomic<std::uint64_t> responses_3xx{0};
    std::atomic<std::uint64_t> responses_4xx{0};
    std::atomic<std::uint64_t> responses_5xx{0};

    // --- 连接 ---
    std::atomic<std::uint64_t> connections_total{0};
    std::atomic<std::int64_t>  connections_active{0};

    // --- 延迟直方图 bucket（累积，Prometheus 风格）---
    std::atomic<std::uint64_t> latency_le_1ms{0};     // ≤ 1ms
    std::atomic<std::uint64_t> latency_le_5ms{0};     // ≤ 5ms
    std::atomic<std::uint64_t> latency_le_10ms{0};    // ≤ 10ms
    std::atomic<std::uint64_t> latency_le_50ms{0};    // ≤ 50ms
    std::atomic<std::uint64_t> latency_le_100ms{0};   // ≤ 100ms
    std::atomic<std::uint64_t> latency_le_500ms{0};   // ≤ 500ms
    std::atomic<std::uint64_t> latency_le_1s{0};      // ≤ 1s
    std::atomic<std::uint64_t> latency_le_5s{0};      // ≤ 5s
    std::atomic<std::uint64_t> latency_le_inf{0};     // +Inf

    // --- 延迟累积 ---
    std::atomic<std::uint64_t> latency_sum_us{0};     // 微秒总和

    // --- body 大小 ---
    std::atomic<std::uint64_t> response_bytes_total{0};

    /// 记录一个请求的延迟（微秒）
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

    /// 服务启动时间
    [[nodiscard]] auto uptime_seconds() const noexcept -> double {
        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        return std::chrono::duration<double>(elapsed).count();
    }

    /// 序列化为 Prometheus text exposition format
    [[nodiscard]] auto serialize() const -> std::string {
        auto total = requests_total.load(std::memory_order_relaxed);
        auto sum_us = latency_sum_us.load(std::memory_order_relaxed);
        double sum_s = static_cast<double>(sum_us) / 1'000'000.0;

        std::string out;
        out.reserve(2048);

        // 进程运行时间
        out += std::format(
            "# HELP cnetmod_uptime_seconds Process uptime.\n"
            "# TYPE cnetmod_uptime_seconds gauge\n"
            "cnetmod_uptime_seconds {:.3f}\n", uptime_seconds());

        // 请求总数
        out += std::format(
            "# HELP cnetmod_http_requests_total Total HTTP requests.\n"
            "# TYPE cnetmod_http_requests_total counter\n"
            "cnetmod_http_requests_total {}\n", total);

        // 按状态码分类
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

        // 连接
        out += std::format(
            "# HELP cnetmod_connections_active Current active connections.\n"
            "# TYPE cnetmod_connections_active gauge\n"
            "cnetmod_connections_active {}\n",
            connections_active.load(std::memory_order_relaxed));

        // 延迟直方图
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

        // 响应 body 总字节数
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
// metrics_middleware — 自动统计中间件
// =============================================================================

export inline auto metrics_middleware(metrics_collector& mc)
    -> http::middleware_fn
{
    return [&mc](http::request_context& ctx, http::next_fn next) -> task<void> {
        mc.requests_total.fetch_add(1, std::memory_order_relaxed);

        auto start = std::chrono::steady_clock::now();

        co_await next();

        // 延迟
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
        mc.record_latency(us);

        // 状态码分类
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 300)
            mc.responses_2xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 300 && status < 400)
            mc.responses_3xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 400 && status < 500)
            mc.responses_4xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 500)
            mc.responses_5xx.fetch_add(1, std::memory_order_relaxed);

        // body 大小
        mc.response_bytes_total.fetch_add(
            ctx.resp().body().size(), std::memory_order_relaxed);
    };
}

// =============================================================================
// metrics_handler — 暴露 /metrics 端点
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
