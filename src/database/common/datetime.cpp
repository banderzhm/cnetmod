module cnetmod.database.datetime;

namespace cnetmod::database {
namespace {

    constexpr std::int64_t minimum_supported_unix_seconds = -62'135'596'800LL;
    constexpr std::int64_t maximum_supported_unix_seconds = 253'402'300'799LL;

} // namespace

auto datetime_from_unix_seconds(std::int64_t value) noexcept
    -> std::optional<calendar_datetime>
{
    if (value < minimum_supported_unix_seconds ||
        value > maximum_supported_unix_seconds)
        return std::nullopt;

    using namespace std::chrono;
    const sys_seconds instant{seconds{value}};
    const auto day = floor<days>(instant);
    const year_month_day date{day};
    if (!date.ok())
        return std::nullopt;

    const hh_mm_ss time{instant - day};
    return calendar_datetime{
        .year = static_cast<std::uint16_t>(static_cast<int>(date.year())),
        .month = static_cast<std::uint8_t>(static_cast<unsigned>(date.month())),
        .day = static_cast<std::uint8_t>(static_cast<unsigned>(date.day())),
        .hour = static_cast<std::uint8_t>(time.hours().count()),
        .minute = static_cast<std::uint8_t>(time.minutes().count()),
        .second = static_cast<std::uint8_t>(time.seconds().count()),
        .microsecond = 0,
    };
}

auto unix_seconds_from_datetime(const calendar_datetime& value) noexcept
    -> std::optional<std::int64_t>
{
    using namespace std::chrono;
    const year_month_day date{
        year{value.year}, month{value.month}, day{value.day}};
    if (!date.ok() || value.hour > 23 || value.minute > 59 ||
        value.second > 59 || value.microsecond > 999'999)
        return std::nullopt;

    const auto instant = sys_days{date} + hours{value.hour} +
        minutes{value.minute} + seconds{value.second};
    return duration_cast<seconds>(instant.time_since_epoch()).count();
}

auto unix_seconds_from_datetime(
    const std::optional<calendar_datetime>& value) noexcept
    -> std::optional<std::int64_t>
{
    return value ? unix_seconds_from_datetime(*value) : std::nullopt;
}

} // namespace cnetmod::database
