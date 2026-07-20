module;

module cnetmod.utils.charconv;

import std;

namespace cnetmod {

auto from_chars_double(std::string_view value, double& result) -> std::errc {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
    return std::from_chars(value.data(), value.data() + value.size(), result).ec;
#else
    try {
        result = std::stod(std::string(value));
        return {};
    } catch (...) {
        return std::errc::invalid_argument;
    }
#endif
}

auto from_chars_float(std::string_view value, float& result) -> std::errc {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
    return std::from_chars(value.data(), value.data() + value.size(), result).ec;
#else
    try {
        result = std::stof(std::string(value));
        return {};
    } catch (...) {
        return std::errc::invalid_argument;
    }
#endif
}

} // namespace cnetmod