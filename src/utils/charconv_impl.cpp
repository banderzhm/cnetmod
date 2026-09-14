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

} // namespace cnetmod
