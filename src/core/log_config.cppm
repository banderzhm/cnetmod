/// Public logger configuration and extension contracts.
export module cnetmod.core.log:config;

import std;

export namespace logger {
enum class level
{
    trace,
    debug,
    info,
    warn,
    error,
    critical,
    off
};

enum class output_format
{
    text,
    json
};

struct rotation_options
{
    std::size_t max_file_size = 0; // 0 disables size-based rotation
    std::size_t max_files = 0;     // 0 retains all rotated files
    bool daily = false;
};

using sink = std::function<void(std::string_view)>;

/**
 * @brief Immutable metadata for one emitted logger event.
 *
 * Views are valid only for the duration of an observer callback. Observers
 * that retain data must make their own copies.
 */
struct log_record
{
    level severity{};
    std::string_view message;
    std::string_view source;
    std::string_view thread_id;
    std::string_view trace_id;
    std::string_view span_id;
    std::chrono::system_clock::time_point observed_at;
};

/**
 * @brief Explicit distributed-trace identity attached to one log event.
 *
 * The logger treats these as opaque values. It does not import an
 * observability implementation or store process-global active-span state.
 */
struct log_correlation
{
    std::string_view trace_id;
    std::string_view span_id;
};

/**
 * @brief Receives completed logger events on the logger worker thread.
 *
 * An observer must be non-blocking and must not call logger APIs recursively.
 * Exceptions are isolated by the logger and never affect other observers.
 */
using observer = std::function<void(const log_record&)>;
using observer_id = std::uint64_t;
} // namespace logger
