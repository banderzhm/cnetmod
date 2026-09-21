module;

#include <glaze/json/generic.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

/**
 * @brief Public JSON codec SPI with Glaze as the default implementation.
 */
export module cnetmod.json;

import std;

#if defined(_MSC_VER)
// MSVC 19.51 does not retain the nested namespace names referenced by
// Glaze's exported writer templates when their declarations originate in the
// global module fragment. Re-exporting the namespace names keeps those
// otherwise reachable helper declarations available to importer-side template
// instantiations without exposing or copying Glaze's implementation members.
export namespace glz {
namespace itoa_impl {}

namespace itoa_40kb_impl {}
} // namespace glz
#endif

export namespace cnetmod::json {

/**
 * @brief Owns an unmodified JSON subtree for compatibility boundaries.
 */
using raw_value = glz::raw_json;

/**
 * @brief Owns a dynamically shaped JSON document at an external-provider boundary.
 *
 * Business requests and responses should use concrete DTOs. This type is reserved
 * for schemas owned by providers that cannot be represented by a stable framework
 * contract, while still keeping all JSON work on the Glaze implementation.
 */
using value = glz::generic_u64;

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
 * @brief Glaze policy for projections that intentionally ignore unknown keys.
 *
 * This policy is intended for version-tolerant reads of externally owned JSON
 * documents. Strict request DTOs should continue to use `glaze_codec`.
 */
struct lenient_glaze_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        T value{};
        constexpr glz::opts options{.error_on_unknown_keys = false};
        if (const auto error = glz::read<options>(value, input); error)
            return std::unexpected(make_error_code(errc::parse_failed));
        return value;
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        return glaze_codec::template encode<T>(value);
    }
};

/**
 * @brief Glaze policy that preserves nullable members as explicit JSON nulls.
 *
 * Use this policy for published contracts where a missing member and a member
 * with a null value have different meanings. Request decoding remains strict.
 */
struct explicit_null_glaze_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        return glaze_codec::template decode<T>(input);
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        constexpr glz::opts options{.skip_null_members = false};
        auto encoded = glz::write<options>(value);
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
