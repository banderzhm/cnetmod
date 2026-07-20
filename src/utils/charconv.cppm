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
