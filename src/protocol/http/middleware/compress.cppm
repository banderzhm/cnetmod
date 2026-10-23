export module cnetmod.protocol.http.middleware.compress;

import std;

import cnetmod.protocol.http;
import cnetmod.instrumentation.metric;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.http.semantics;

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
 * @brief Returns the semantic identity of a repeatable response.
 *
 * The key must include every request property that can change the response
 * body. Returning `std::nullopt` bypasses the cache for that request. The
 * middleware also compares the uncompressed body before reusing an entry, so
 * stale application data is recompressed instead of being served.
 */
using compression_cache_key = std::function<std::optional<std::string>(
    const http::request_context&)>;

/**
 * @brief Configures response compression policy and optional infrastructure.
 */
struct compress_options
{
    /** @brief Smallest response body eligible for compression. */
    std::size_t min_size = 1024;

    /** @brief Gzip compression level passed to the selected backend. */
    int level = 6;

    /**
     * @brief Brotli quality (0-11) for clients that accept `br`, or a
     * negative value to negotiate gzip only (the default). When enabled, `br`
     * is chosen whenever the client rates it at least as high as gzip.
     * Ignored when the library was built without Brotli.
     */
    int brotli_quality = -1;

    /** @brief Brotli window size, log2 bytes (10-24). */
    int brotli_window = 18;

    /** @brief Maximum number of compression operations in flight. */
    std::size_t max_concurrency = 4;

    /**
     * @brief Maximum number of explicitly keyed compressed responses kept in
     * memory. Zero disables caching (the default).
     */
    std::size_t cache_capacity = 0;

    /**
     * @brief Optional semantic key for repeatable response compression.
     *
     * Caching remains disabled unless both this callback and `cache_capacity`
     * are configured. Do not key personalized responses only by their route.
     */
    compression_cache_key cache_key;

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
 * @brief Creates gzip (and optionally Brotli) response compression with
 * negotiation and bounded work.
 */
[[nodiscard]] auto compress(compress_options opts = {}) -> http::middleware_fn;
} // namespace cnetmod
