module cnetmod.instrumentation.metric_aggregation;

import std;
import cnetmod.instrumentation.metric;

namespace cnetmod::instrumentation {

metric_aggregation::metric_aggregation(std::size_t instrument_limit,
    std::size_t attribute_limit)
    : instrument_limit_(instrument_limit), attribute_limit_(attribute_limit), started_at_(std::chrono::system_clock::now())
{
}

auto metric_aggregation::record(metric_measurement measurement) -> bool
{
    if (!valid_metric_measurement(measurement))
        return false;
    std::ranges::sort(measurement.attributes);
    for (std::size_t index{}; index < measurement.attributes.size(); ++index)
    {
        const auto& key = measurement.attributes[index].first;
        if (key.empty() || key == "otel.metric.overflow" ||
            (index > 0U && key == measurement.attributes[index - 1U].first))
            return false;
    }
    auto found = instruments_.find(measurement.name);
    if (found == instruments_.end())
    {
        if (instruments_.size() >= instrument_limit_)
            return false;
        found = instruments_.emplace(measurement.name,
                                instrument{.kind = measurement.kind, .unit = measurement.unit, .explicit_bounds = measurement.explicit_bounds})
                    .first;
    }
    auto& target = found->second;
    if (target.kind != measurement.kind || target.unit != measurement.unit ||
        target.explicit_bounds != measurement.explicit_bounds)
        return false;

    metric_point* point{};
    const auto existing = target.points.find(measurement.attributes);
    if (existing != target.points.end())
        point = &existing->second;
    else
    {
        metric_point initial{
            .started_at = std::min(started_at_, measurement.observed_at),
            .observed_at = measurement.observed_at,
        };
        if (measurement.kind == metric_kind::histogram &&
            (target.points.size() < attribute_limit_ || !target.overflow))
            initial.bucket_counts.resize(target.explicit_bounds.size() + 1U);
        if (target.points.size() < attribute_limit_)
        {
            initial.attributes = measurement.attributes;
            point = &target.points.emplace(std::move(measurement.attributes),
                                      std::move(initial))
                         .first->second;
        }
        else
        {
            if (!target.overflow)
            {
                initial.overflow = true;
                target.overflow.emplace(std::move(initial));
            }
            point = &*target.overflow;
        }
    }
    if (measurement.kind == metric_kind::histogram)
    {
        if (point->count == std::numeric_limits<std::uint64_t>::max())
            return false;
        const bool has_negative = point->has_negative || measurement.value < 0.0;
        const double sum = has_negative ? 0.0 : point->value + measurement.value;
        if (!std::isfinite(sum))
            return false;
        const auto bucket = static_cast<std::size_t>(
            std::ranges::lower_bound(target.explicit_bounds, measurement.value) - target.explicit_bounds.begin());
        point->minimum = point->count == 0U ? measurement.value : std::min(point->minimum, measurement.value);
        point->maximum = point->count == 0U ? measurement.value : std::max(point->maximum, measurement.value);
        ++point->count;
        ++point->bucket_counts[bucket];
        point->value = sum;
        point->has_negative = has_negative;
    }
    else if (measurement.kind == metric_kind::counter)
    {
        const double sum = point->value + measurement.value;
        if (!std::isfinite(sum))
            return false;
        point->value = sum;
    }
    else if (measurement.observed_at >= point->observed_at)
        point->value = measurement.value;
    point->observed_at = std::max(point->observed_at, measurement.observed_at);
    return true;
}

auto metric_aggregation::collect() const -> std::vector<metric_series>
{
    std::vector<metric_series> result;
    result.reserve(instruments_.size());
    for (const auto& [name, instrument] : instruments_)
    {
        metric_series series{.name = name, .kind = instrument.kind, .unit = instrument.unit, .explicit_bounds = instrument.explicit_bounds};
        series.points.reserve(instrument.points.size() + (instrument.overflow ? 1U : 0U));
        for (const auto& [attributes, point] : instrument.points)
            series.points.push_back(point);
        if (instrument.overflow)
            series.points.push_back(*instrument.overflow);
        result.push_back(std::move(series));
    }
    return result;
}

auto metric_aggregation::instrument_count() const noexcept -> std::size_t
{
    return instruments_.size();
}

} // namespace cnetmod::instrumentation
