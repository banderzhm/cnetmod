export module cnetmod.protocol.http.middleware.compress;

import std;
import cnetmod.instrumentation.metric;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.http;

export namespace cnetmod {

/**
 * @brief Owns one CPU compression operation until it has completed.
 */
using compression_operation =
    std::function<std::expected<std::string, std::error_code>()>;

/**
 * @brief Schedules CPU compression and resumes on the request executor.
 *
 * Implementations must not invoke the operation after the returned task has
 * completed. Cancellation may discard a completed result, but it cannot
 * forcibly interrupt a compression library call already executing.
 */
using compression_dispatch = std::function<task<std::expected<std::string,
    std::error_code>>(compression_operation, cancel_token&)>;

/**
 * @brief Configures response compression policy and optional infrastructure.
 */
struct compress_options
{
    /** @brief Smallest response body eligible for compression. */
    std::size_t min_size = 1024;

    /** @brief Gzip compression level passed to the selected backend. */
    int level = 6;

    /** @brief Maximum number of compression operations in flight. */
    std::size_t max_concurrency = 4;

    /**
     * @brief Optional CPU scheduler supplied by the owning runtime.
     *
     * An empty dispatcher preserves the standalone synchronous behavior.
     * Application hosts should always provide their managed CPU dispatcher.
     */
    compression_dispatch dispatch;

    /** @brief Optional nonblocking sink for compression measurements. */
    instrumentation::metric_sink measurements;
};

/**
 * @brief Creates gzip response compression with negotiation and bounded work.
 */
[[nodiscard]] auto compress(compress_options opts = {}) -> http::middleware_fn;
} // namespace cnetmod
