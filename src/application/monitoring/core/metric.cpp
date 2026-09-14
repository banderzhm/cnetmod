module cnetmod.instrumentation.metric;

import std;

namespace cnetmod::instrumentation {

auto valid_metric_measurement(const metric_measurement& measurement) noexcept -> bool
{
    if (measurement.name.empty() || !std::isfinite(measurement.value))
        return false;
    switch (measurement.kind)
    {
    case metric_kind::counter:
        return measurement.value >= 0.0 && measurement.explicit_bounds.empty();
    case metric_kind::gauge:
        return measurement.explicit_bounds.empty();
    case metric_kind::histogram:
        if (measurement.explicit_bounds.size() > 256U)
            return false;
        for (std::size_t index{}; index < measurement.explicit_bounds.size(); ++index)
        {
            const auto bound = measurement.explicit_bounds[index];
            if (!std::isfinite(bound) ||
                (index > 0U && bound <= measurement.explicit_bounds[index - 1U]))
                return false;
        }
        return true;
    }
    return false;
}

} // namespace cnetmod::instrumentation
