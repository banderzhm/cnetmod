/// Bounded, asynchronous OTLP/HTTP JSON exporter.
///
/// It intentionally receives completed spans as explicit values. No
/// thread-local trace scope is used, so work that resumes on another coroutine
/// worker cannot leak an unrelated trace.
export module cnetmod.observability.otlp;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::observability {

export struct otlp_http_options
{
    /// Full collector endpoint, normally http://collector:4318/v1/traces.
    std::string endpoint;
    std::string service_name{"cnetmod"};
    std::size_t queue_capacity{4096};
    std::size_t max_batch_size{256};
    std::chrono::milliseconds request_timeout{5000};
};

export struct otlp_exporter_statistics
{
    std::uint64_t accepted{};
    std::uint64_t dropped{};
    std::uint64_t exported{};
    std::uint64_t failed_batches{};
};

class otlp_http_exporter_state;

/// Thread-safe producer API backed by cnetmod's bounded lock-free MPMC queue.
/// Sending is always performed on the supplied io_context; submit() never
/// blocks an application thread or makes an HTTP request inline.
export class otlp_http_exporter
{
public:
    otlp_http_exporter(io_context& context, otlp_http_options options);
    ~otlp_http_exporter();
    otlp_http_exporter(const otlp_http_exporter&) = delete;
    auto operator=(const otlp_http_exporter&) -> otlp_http_exporter& = delete;
    otlp_http_exporter(otlp_http_exporter&&) noexcept = default;
    auto operator=(otlp_http_exporter&&) noexcept -> otlp_http_exporter& = default;

    /// Returns false only when the bounded queue is full or the exporter has
    /// been closed. Drops are counted and intentionally do not affect the
    /// request whose span is being observed.
    [[nodiscard]] auto submit(http::tracing::completed_span span) noexcept -> bool;
    /// Wait until all spans accepted before this call have either been handed
    /// to the collector or accounted for as a failed batch.  `flush()` is
    /// intended for orderly process shutdown and tests; request handlers
    /// should keep using the non-blocking `submit()` path.
    [[nodiscard]] auto flush(std::chrono::milliseconds timeout =
                                 std::chrono::seconds{5})
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto statistics() const noexcept -> otlp_exporter_statistics;
    /// Stop accepting new spans and schedule delivery of the already accepted
    /// queue.  Use `flush()` before destroying the surrounding io_context
    /// when graceful delivery is required.
    void close() noexcept;

private:
    std::shared_ptr<otlp_http_exporter_state> state_;
};

} // namespace cnetmod::observability
