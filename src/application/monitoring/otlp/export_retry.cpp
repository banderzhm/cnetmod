module cnetmod.observability.export_retry;

import std;

namespace cnetmod::observability::detail {

auto export_retry_delay(std::chrono::milliseconds initial,
    std::chrono::milliseconds maximum, std::size_t attempt,
    std::string_view retry_after) noexcept -> std::chrono::milliseconds
{
    using duration = std::chrono::milliseconds;
    const auto cap = static_cast<std::uint64_t>(std::max(maximum, duration::zero()).count());
    if (!retry_after.empty())
    {
        std::uint64_t seconds{};
        const auto parsed = std::from_chars(retry_after.data(),
            retry_after.data() + retry_after.size(), seconds);
        if (parsed.ptr == retry_after.data() + retry_after.size())
        {
            if (parsed.ec == std::errc::result_out_of_range)
                return duration{cap};
            if (parsed.ec == std::errc{})
                return duration{seconds > cap / 1000U ? cap : seconds * 1000U};
        }
    }
    const auto base = static_cast<std::uint64_t>(std::clamp(initial, duration::zero(), duration{cap}).count());
    if (base == 0U)
        return duration::zero();
    if (attempt >= std::numeric_limits<std::uint64_t>::digits)
        return duration{cap};
    const auto multiplier = std::uint64_t{1} << attempt;
    return duration{base > cap / multiplier ? cap : base * multiplier};
}

} // namespace cnetmod::observability::detail
