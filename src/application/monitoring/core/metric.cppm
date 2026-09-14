/**
 * @brief Transport-independent metric measurements and aggregated data points.
 */
export module cnetmod.instrumentation.metric;

import std;

namespace cnetmod::instrumentation {

export enum class metric_kind
{
    gauge,
    counter,
    histogram
};

/**
 * @brief A gauge observation, counter increment, or histogram observation.
 *
 * Counter values are increments, not cumulative snapshots. Attributes identify
 * a time series; their order does not affect its identity.
 */
export struct metric_measurement
{
    std::string name;
    double value{};
    metric_kind kind = metric_kind::gauge;
    std::string unit;
    std::chrono::system_clock::time_point observed_at = std::chrono::system_clock::now();
    std::vector<std::pair<std::string, std::string>> attributes;
    /**
     * @brief Strictly increasing finite histogram upper bounds (at most 256).
     *
     * Empty bounds select one bucket covering all values. Non-histogram
     * measurements must leave this field empty. Bounds remain fixed for the
     * lifetime of an instrument. Bucket upper bounds are inclusive.
     */
    std::vector<double> explicit_bounds;
};

/**
 * @brief A cumulative sum, histogram, or latest gauge for one attribute set.
 *
 * Histogram count and buckets include every accepted observation. Once a
 * negative value is observed, has_negative remains true and no histogram sum
 * is exported. Min, max, count, and buckets remain available.
 */
export struct metric_point
{
    double value{};
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point observed_at;
    std::vector<std::pair<std::string, std::string>> attributes;
    bool overflow{};
    std::uint64_t count{};
    std::vector<std::uint64_t> bucket_counts;
    double minimum{};
    double maximum{};
    bool has_negative{};
};

/**
 * @brief All aggregated points for one instrument, emitted as one OTLP metric.
 */
export struct metric_series
{
    std::string name;
    metric_kind kind{};
    std::string unit;
    std::vector<metric_point> points;
    std::vector<double> explicit_bounds;
};

/**
 * @brief Validates numeric values and bucket configuration without allocation.
 */
export [[nodiscard]] auto valid_metric_measurement(const metric_measurement& measurement) noexcept -> bool;

/**
 * @brief Nonblocking measurement consumer; an empty sink disables production.
 */
export using metric_sink = std::function<void(metric_measurement)>;

} // namespace cnetmod::instrumentation
