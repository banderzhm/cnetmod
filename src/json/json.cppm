module;

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

/**
 * @brief Public JSON codec SPI with Glaze as the default implementation.
 */
export module cnetmod.json;

import std;

export namespace cnetmod::json {

/**
 * @brief Portable failures produced by JSON codecs.
 */
enum class errc
{
    parse_failed = 1,
    serialization_failed
};

/**
 * @brief Creates the error code associated with a JSON codec failure.
 */
[[nodiscard]] auto make_error_code(errc value) noexcept -> std::error_code;

/**
 * @brief Describes a stateless JSON codec usable by the public JSON facade.
 */
template <typename Codec, typename T>
concept codec_for = requires(std::string_view input, const T& value) {
    { Codec::template decode<T>(input) }
    -> std::same_as<std::expected<T, std::error_code>>;
    { Codec::template encode<T>(value) }
    -> std::same_as<std::expected<std::string, std::error_code>>;
};

/**
 * @brief Glaze-backed default JSON policy.
 */
struct glaze_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        T value{};
        if (const auto error = glz::read_json(value, input); error)
            return std::unexpected(make_error_code(errc::parse_failed));
        return value;
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        auto encoded = glz::write_json(value);
        if (!encoded)
            return std::unexpected(
                make_error_code(errc::serialization_failed));
        return std::move(*encoded);
    }
};

/**
 * @brief Parses a JSON document with the selected codec policy.
 */
template <typename T, typename Codec = glaze_codec>
requires codec_for<Codec, T>
[[nodiscard]] auto parse(std::string_view input)
    -> std::expected<T, std::error_code>
{
    return Codec::template decode<T>(input);
}

/**
 * @brief Serializes a value with the selected codec policy.
 */
template <typename T, typename Codec = glaze_codec>
requires codec_for<Codec, T>
[[nodiscard]] auto write(const T& value)
    -> std::expected<std::string, std::error_code>
{
    return Codec::template encode<T>(value);
}

} // namespace cnetmod::json

export template <>
struct std::is_error_code_enum<cnetmod::json::errc> : std::true_type
{
};
