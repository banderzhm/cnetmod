/**
 * @file charconv.cppm
 * @brief Cross-platform character conversion utilities
 * 
 * Provides wrappers for std::from_chars with fallback for platforms
 * where floating-point support is not available (e.g., macOS < 26.0)
 */
export module cnetmod.utils:charconv;

import std;

namespace cnetmod {

// =============================================================================
// from_chars wrapper with fallback for floating-point types
// =============================================================================

/**
 * @brief Parse a double from string view with cross-platform compatibility
 * 
 * Uses std::from_chars when available, falls back to std::stod on platforms
 * where floating-point from_chars is not supported (e.g., macOS < 26.0)
 * 
 * @param sv String view to parse
 * @param value Output value
 * @return std::errc::invalid_argument on parse error, std::errc{} on success
 */
export inline auto from_chars_double(std::string_view sv, double& value) -> std::errc {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
    // std::from_chars for floating point is available
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    return result.ec;
#else
    // Fallback to std::stod for platforms without from_chars floating-point support
    try {
        value = std::stod(std::string(sv));
        return std::errc{};
    } catch (...) {
        return std::errc::invalid_argument;
    }
#endif
}

/**
 * @brief Parse a float from string view with cross-platform compatibility
 */
export inline auto from_chars_float(std::string_view sv, float& value) -> std::errc {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    return result.ec;
#else
    try {
        value = std::stof(std::string(sv));
        return std::errc{};
    } catch (...) {
        return std::errc::invalid_argument;
    }
#endif
}

/**
 * @brief Parse an integer from string view (always uses std::from_chars)
 * 
 * Integer support in from_chars is widely available
 */
export template<std::integral T>
inline auto from_chars_int(std::string_view sv, T& value, int base = 10) -> std::errc {
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value, base);
    return result.ec;
}

} // namespace cnetmod
