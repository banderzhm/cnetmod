/**
 * @brief Overflow-safe OTLP exporter retry delay policy.
 */
export module cnetmod.observability.export_retry;

import std;

namespace cnetmod::observability::detail {

/**
 * @brief Selects a bounded Retry-After delay or exponential fallback.
 *
 * Numeric Retry-After values use seconds and saturate before conversion to
 * milliseconds. Invalid headers use exponential backoff. Negative configured
 * durations are treated as zero. No allocation or network operation occurs.
 */
export [[nodiscard]] auto export_retry_delay(std::chrono::milliseconds initial,
    std::chrono::milliseconds maximum, std::size_t attempt,
    std::string_view retry_after = {}) noexcept -> std::chrono::milliseconds;

} // namespace cnetmod::observability::detail
