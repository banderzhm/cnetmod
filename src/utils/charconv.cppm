/**
 * @file charconv.cppm
 * @brief Cross-platform character conversion utilities
 *
 * Provides wrappers for std::from_chars with fallback for platforms
 * where floating-point support is not available (e.g., macOS < 26.0)
 */
export module cnetmod.utils.charconv;

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
export auto from_chars_double(std::string_view sv, double& value) -> std::errc;

/**
 * @brief Parse a float from string view with cross-platform compatibility
 */
export auto from_chars_float(std::string_view sv, float& value) -> std::errc;

/**
 * @brief Formats a double with cross-platform floating-point support.
 *
 * The output range follows std::to_chars semantics. On Apple platforms the
 * implementation uses the standard formatting library because older libc++
 * releases do not provide floating-point std::to_chars.
 */
export auto to_chars_double(char* first, char* last, double value,
    int precision = std::numeric_limits<double>::max_digits10)
    -> std::to_chars_result;

/**
 * @brief Formats a float with cross-platform floating-point support.
 */
export auto to_chars_float(char* first, char* last, float value,
    int precision = std::numeric_limits<float>::max_digits10)
    -> std::to_chars_result;

/**
 * @brief Parse an integer from string view (always uses std::from_chars)
 *
 * Integer support in from_chars is widely available
 */
export template <std::integral T>
inline auto from_chars_int(std::string_view sv, T& value, int base = 10) -> std::errc
{
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value, base);
    return result.ec;
}

} // namespace cnetmod
