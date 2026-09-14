/**
 * @brief Bounded, transport-independent cumulative metric aggregation.
 */
export module cnetmod.instrumentation.metric_aggregation;

import std;
export import cnetmod.instrumentation.metric;

namespace cnetmod::instrumentation {

/**
 * @brief Aggregates measurements on a single owning executor.
 *
 * Each instrument retains at most attribute_limit ordinary series and one
 * overflow series. New instruments beyond instrument_limit are rejected;
 * existing instruments remain usable. Collection never resets cumulative sums.
 * This class performs no I/O and has no internal locks.
 */
export class metric_aggregation
{
public:
    explicit metric_aggregation(std::size_t instrument_limit = 128,
        std::size_t attribute_limit = 256);

    /**
     * @brief Records one measurement without changing existing instrument identity.
     * @return false for invalid data, conflicting definitions, exhausted
     * instrument capacity, or a sum that would cease to be finite.
     */
    [[nodiscard]] auto record(metric_measurement measurement) -> bool;

    /**
     * @brief Copies all retained series without resetting their start times.
     */
    [[nodiscard]] auto collect() const -> std::vector<metric_series>;

    [[nodiscard]] auto instrument_count() const noexcept -> std::size_t;

private:
    struct instrument
    {
        metric_kind kind{};
        std::string unit;
        std::map<std::vector<std::pair<std::string, std::string>>, metric_point> points;
        std::optional<metric_point> overflow;
        std::vector<double> explicit_bounds;
    };

    std::size_t instrument_limit_;
    std::size_t attribute_limit_;
    std::chrono::system_clock::time_point started_at_;
    std::map<std::string, instrument, std::less<>> instruments_;
};

} // namespace cnetmod::instrumentation
