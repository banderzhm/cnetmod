module cnetmod.integration.mongodb_cloud;
import std;
import cnetmod.protocol.mongodb;

namespace cnetmod::mongodb::cloud::detail {
auto percent_encode(std::string_view value) -> std::string
{
    constexpr std::string_view hex{"0123456789ABCDEF"};
    std::string result;
    for (unsigned char c : value)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            result += static_cast<char>(c);
        else
        {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}

auto parse_utc_expiry(std::string_view value) -> result<std::chrono::system_clock::time_point>
{
    const auto invalid = []
    {
        return std::unexpected(make_error(error_code::authentication_failed, "invalid cloud credential expiration"));
    };
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':')
        return invalid();
    auto suffix = value.substr(19);
    if (suffix.starts_with('.'))
    {
        const auto end = suffix.find_first_not_of("0123456789", 1);
        if (end == std::string_view::npos || end == 1 || end > 10)
            return invalid();
        suffix.remove_prefix(end);
    }
    constexpr std::string_view utc_z{"Z"}, utc_offset{"+00:00"};
    if (suffix != utc_z && suffix != utc_offset)
        return invalid();
    const auto number = [&](std::size_t offset, std::size_t size) -> int
    {
        int result{};
        auto [end, error] = std::from_chars(value.data() + offset, value.data() + offset + size, result);
        return error == std::errc{} && end == value.data() + offset + size ? result : -1;
    };
    const auto year = number(0, 4), month = number(5, 2), day = number(8, 2), hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)}, std::chrono::day{static_cast<unsigned>(day)}};
    if (!date.ok() || year < 1970 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return invalid();
    const auto time = std::chrono::sys_days{date} + std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
    if (time.time_since_epoch() > std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::duration::max()))
        return invalid();
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(time);
}
} // namespace cnetmod::mongodb::cloud::detail
