#include "test_framework.hpp"

import std;
import cnetmod.instrumentation.metric;
import cnetmod.instrumentation.metric_aggregation;

namespace ci = cnetmod::instrumentation;

TEST(counters_accumulate_across_collections_with_stable_start_time)
{
    ci::metric_aggregation aggregation;
    auto time = std::chrono::system_clock::now();
    ASSERT_TRUE(aggregation.record({.name = "requests", .value = 2, .kind = ci::metric_kind::counter, .observed_at = time, .attributes = {{"method", "GET"}, {"route", "/orders"}}}));
    auto first = aggregation.collect();
    ASSERT_EQ(first.size(), 1U);
    ASSERT_EQ(first[0].points[0].value, 2.0);
    ASSERT_TRUE(aggregation.record({.name = "requests", .value = 3, .kind = ci::metric_kind::counter, .observed_at = time + std::chrono::seconds{1}, .attributes = {{"route", "/orders"}, {"method", "GET"}}}));
    auto second = aggregation.collect();
    ASSERT_EQ(second[0].points.size(), 1U);
    ASSERT_EQ(second[0].points[0].value, 5.0);
    ASSERT_TRUE(second[0].points[0].started_at == first[0].points[0].started_at);
    ASSERT_TRUE(second[0].points[0].observed_at > first[0].points[0].observed_at);
    ASSERT_EQ(aggregation.collect()[0].points[0].value, 5.0);
}

TEST(attribute_overflow_preserves_totals_and_existing_series)
{
    ci::metric_aggregation aggregation{1, 1};
    for (const auto& tenant : {"first", "second", "third", "first"})
        ASSERT_TRUE(aggregation.record({.name = "requests", .value = 1, .kind = ci::metric_kind::counter, .attributes = {{"tenant", tenant}}}));
    auto result = aggregation.collect();
    ASSERT_EQ(result[0].points.size(), 2U);
    ASSERT_EQ(result[0].points[0].value, 2.0);
    ASSERT_FALSE(result[0].points[0].overflow);
    ASSERT_EQ(result[0].points[1].value, 2.0);
    ASSERT_TRUE(result[0].points[1].overflow);
    ASSERT_TRUE(result[0].points[1].attributes.empty());
    ASSERT_FALSE(aggregation.record({.name = "another", .value = 1}));
    ASSERT_EQ(aggregation.instrument_count(), 1U);
    ci::metric_aggregation only_overflow{1, 0};
    ASSERT_TRUE(only_overflow.record({.name = "requests", .value = 7, .kind = ci::metric_kind::counter}));
    ASSERT_TRUE(only_overflow.collect()[0].points[0].overflow);
}

TEST(gauges_keep_latest_observation_instead_of_accumulating)
{
    ci::metric_aggregation aggregation;
    const auto time = std::chrono::system_clock::now();
    ASSERT_TRUE(aggregation.record({.name = "depth", .value = 10, .observed_at = time}));
    ASSERT_TRUE(aggregation.record({.name = "depth", .value = -2, .observed_at = time + std::chrono::seconds{1}}));
    ASSERT_TRUE(aggregation.record({.name = "depth", .value = 99, .observed_at = time}));
    ASSERT_EQ(aggregation.collect()[0].points[0].value, -2.0);
}

TEST(invalid_or_conflicting_measurements_preserve_existing_state)
{
    ci::metric_aggregation aggregation;
    ASSERT_FALSE(aggregation.record({.name = "requests", .value = -1, .kind = ci::metric_kind::counter}));
    ASSERT_FALSE(aggregation.record({.name = "requests", .attributes = {{"a", "1"}, {"a", "2"}}}));
    ASSERT_FALSE(aggregation.record({.name = "requests", .attributes = {{"otel.metric.overflow", "true"}}}));
    ASSERT_EQ(aggregation.instrument_count(), 0U);
    ASSERT_TRUE(aggregation.record({.name = "requests", .value = 1, .kind = ci::metric_kind::counter, .unit = "{request}"}));
    ASSERT_FALSE(aggregation.record({.name = "requests", .value = 10, .kind = ci::metric_kind::gauge, .unit = "{request}"}));
    ASSERT_FALSE(aggregation.record({.name = "requests", .value = 10, .kind = ci::metric_kind::counter, .unit = "s"}));
    ASSERT_EQ(aggregation.collect()[0].points[0].value, 1.0);
    ASSERT_TRUE(aggregation.record({.name = "large", .value = std::numeric_limits<double>::max(), .kind = ci::metric_kind::counter}));
    ASSERT_FALSE(aggregation.record({.name = "large", .value = std::numeric_limits<double>::max(), .kind = ci::metric_kind::counter}));
    ASSERT_EQ(aggregation.collect()[0].points[0].value, std::numeric_limits<double>::max());
}

TEST(histograms_include_upper_bounds_and_accumulate_across_collections)
{
    ci::metric_aggregation aggregation;
    for (const double value : {0.0, 1.0, 2.0, 3.0})
        ASSERT_TRUE(aggregation.record({.name = "duration", .value = value, .kind = ci::metric_kind::histogram, .explicit_bounds = {1, 2}}));
    auto first = aggregation.collect();
    const auto& point = first[0].points[0];
    ASSERT_EQ(point.count, 4U);
    ASSERT_EQ(point.value, 6.0);
    ASSERT_EQ(point.minimum, 0.0);
    ASSERT_EQ(point.maximum, 3.0);
    ASSERT_TRUE(point.bucket_counts == std::vector<std::uint64_t>({2, 1, 1}));
    ASSERT_TRUE(aggregation.record({.name = "duration", .value = 2, .kind = ci::metric_kind::histogram, .explicit_bounds = {1, 2}}));
    auto next = aggregation.collect();
    ASSERT_EQ(next[0].points[0].count, 5U);
    ASSERT_EQ(next[0].points[0].value, 8.0);
    ASSERT_TRUE(next[0].points[0].started_at == point.started_at);
    ASSERT_TRUE(next[0].points[0].bucket_counts == std::vector<std::uint64_t>({2, 2, 1}));
}

TEST(histogram_overflow_retains_distribution_and_negative_samples_omit_sum)
{
    ci::metric_aggregation aggregation{1, 0};
    for (const double value : {-3.0, -1.0, 2.0})
        ASSERT_TRUE(aggregation.record({.name = "temperature", .value = value, .kind = ci::metric_kind::histogram, .attributes = {{"sensor", std::to_string(value)}}, .explicit_bounds = {-1, 1}}));
    auto series = aggregation.collect();
    const auto& point = series[0].points[0];
    ASSERT_TRUE(point.overflow);
    ASSERT_TRUE(point.has_negative);
    ASSERT_EQ(point.count, 3U);
    ASSERT_EQ(point.minimum, -3.0);
    ASSERT_EQ(point.maximum, 2.0);
    ASSERT_TRUE(point.bucket_counts == std::vector<std::uint64_t>({2, 0, 1}));
}

TEST(histogram_bounds_are_validated_before_allocating_instrument_state)
{
    ci::metric_aggregation aggregation;
    for (auto bounds : {std::vector<double>{2, 1}, std::vector<double>{1, 1},
             std::vector<double>{std::numeric_limits<double>::infinity()},
             std::vector<double>{std::numeric_limits<double>::quiet_NaN()},
             std::vector<double>(257, 0.0)})
        ASSERT_FALSE(aggregation.record({.name = "invalid", .value = 1, .kind = ci::metric_kind::histogram, .explicit_bounds = std::move(bounds)}));
    ASSERT_FALSE(aggregation.record({.name = "gauge", .explicit_bounds = {1}}));
    ASSERT_EQ(aggregation.instrument_count(), 0U);
    ASSERT_TRUE(aggregation.record({.name = "valid", .value = 1, .kind = ci::metric_kind::histogram}));
    ASSERT_FALSE(aggregation.record({.name = "valid", .value = 2, .kind = ci::metric_kind::histogram, .explicit_bounds = {1}}));
    const auto result = aggregation.collect();
    ASSERT_EQ(result[0].points[0].count, 1U);
    ASSERT_TRUE(result[0].points[0].bucket_counts == std::vector<std::uint64_t>({1}));
}

TEST(histogram_sum_overflow_does_not_partially_update_count_or_buckets)
{
    ci::metric_aggregation aggregation;
    ASSERT_TRUE(aggregation.record({.name = "large", .value = std::numeric_limits<double>::max(), .kind = ci::metric_kind::histogram}));
    ASSERT_FALSE(aggregation.record({.name = "large", .value = std::numeric_limits<double>::max(), .kind = ci::metric_kind::histogram}));
    const auto result = aggregation.collect();
    ASSERT_EQ(result[0].points[0].count, 1U);
    ASSERT_EQ(result[0].points[0].value, std::numeric_limits<double>::max());
    ASSERT_TRUE(result[0].points[0].bucket_counts == std::vector<std::uint64_t>({1}));
}

RUN_TESTS()
