module;

module cnetmod.utils.charconv;

import std;

namespace cnetmod {

auto from_chars_double(std::string_view value, double& result) -> std::errc
{
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{})
        return parsed.ec;
    return parsed.ptr == value.data() + value.size()
        ? std::errc{}
        : std::errc::invalid_argument;
#else
    try
    {
        std::size_t consumed = 0;
        result = std::stod(std::string(value), &consumed);
        return consumed == value.size()
            ? std::errc{}
            : std::errc::invalid_argument;
    }
    catch (const std::out_of_range&)
    {
        return std::errc::result_out_of_range;
    }
    catch (...)
    {
        return std::errc::invalid_argument;
    }
#endif
}

auto from_chars_float(std::string_view value, float& result) -> std::errc
{
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{})
        return parsed.ec;
    return parsed.ptr == value.data() + value.size()
        ? std::errc{}
        : std::errc::invalid_argument;
#else
    try
    {
        std::size_t consumed = 0;
        result = std::stof(std::string(value), &consumed);
        return consumed == value.size()
            ? std::errc{}
            : std::errc::invalid_argument;
    }
    catch (const std::out_of_range&)
    {
        return std::errc::result_out_of_range;
    }
    catch (...)
    {
        return std::errc::invalid_argument;
    }
#endif
}

namespace {
    template <typename Value>
    auto format_floating_point(char* first, char* last, Value value,
        int precision) -> std::to_chars_result
    {
        if (first == nullptr || last == nullptr || first > last || precision < 0)
            return {first, std::errc::invalid_argument};
#if defined(__APPLE__)
        const auto capacity = static_cast<std::size_t>(last - first);
        const auto formatted = std::format_to_n(
            first, capacity, "{:.{}g}", value, precision);
        if (formatted.size > capacity)
            return {last, std::errc::value_too_large};
        return {first + formatted.size, std::errc{}};
#else
        return std::to_chars(first, last, value, std::chars_format::general,
            precision);
#endif
    }
} // namespace

auto to_chars_double(char* first, char* last, double value,
    int precision) -> std::to_chars_result
{
    return format_floating_point(first, last, value, precision);
}

auto to_chars_float(char* first, char* last, float value,
    int precision) -> std::to_chars_result
{
    return format_floating_point(first, last, value, precision);
}

} // namespace cnetmod
