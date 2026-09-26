module;

#include <glaze/json/generic.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

/**
 * @brief Glaze-only JSON module facade.
 *
 * The public document type is Glaze's native generic JSON value. Typed reads
 * and writes call Glaze directly and never materialize an intermediate DOM.
 */
export module cnetmod.json;

import std;

// Glaze's parser templates refer to this helper namespace during downstream
// instantiation. Reopen it in the exported module surface so MSVC can resolve
// the namespace name without exposing a second JSON abstraction.
export namespace glz::unicode {
}

export namespace cnetmod::json {

enum class errc
{
    parse_failed = 1,
    serialization_failed,
    type_mismatch,
    missing_field,
    unknown_field
};

[[nodiscard]] auto make_error_code(errc value) noexcept -> std::error_code;

/**
 * @brief Native Glaze dynamic JSON document with lossless integer storage.
 */
using document = glz::generic_u64;
using value = document;
using raw_value = document;
using array_type = document::array_t;
using object_type = document::object_t;

/**
 * @brief Optional document mapping customization point.
 *
 * Framework modules may specialize this trait to preserve domain-specific
 * wire forms while retaining Glaze as the sole JSON parser and writer.
 */
namespace detail {
template <typename T>
struct document_codec;
} // namespace detail

/**
 * @brief Creates an empty native Glaze JSON object.
 */
[[nodiscard]] inline auto object() -> document
{
    document result;
    result = document::object_t{};
    return result;
}

/**
 * @brief Creates a native Glaze JSON object from key/value pairs.
 */
[[nodiscard]] inline auto object(
    std::initializer_list<std::pair<const char*, document>> values) -> document
{
    auto result = object();
    for (const auto& [key, value] : values)
        result[key] = value;
    return result;
}

/**
 * @brief Creates an empty native Glaze JSON array.
 */
[[nodiscard]] inline auto array() -> document
{
    document result;
    result = document::array_t{};
    return result;
}

/**
 * @brief Creates a native Glaze JSON array from values.
 */
[[nodiscard]] inline auto array(std::initializer_list<document> values) -> document
{
    auto result = array();
    result.get_array().assign(values.begin(), values.end());
    return result;
}

[[nodiscard]] auto parse_document(std::string_view input)
    -> std::expected<document, std::error_code>;
[[nodiscard]] auto write_document(const document& value, bool prettify = false)
    -> std::expected<std::string, std::error_code>;

/**
 * @brief Compares two native documents by their canonical compact encoding.
 */
[[nodiscard]] inline auto equivalent(
    const document& left, const document& right) -> bool
{
    const auto left_text = write_document(left);
    const auto right_text = write_document(right);
    return left_text && right_text && *left_text == *right_text;
}

/**
 * @brief Finds an object member without inserting it.
 */
[[nodiscard]] inline auto find(document& source, std::string_view key)
    -> document*
{
    if (!source.is_object())
        return nullptr;
    auto& members = source.get_object();
    const auto found = members.find(key);
    return found == members.end() ? nullptr : std::addressof(found->second);
}

/**
 * @brief Finds a const object member without inserting it.
 */
[[nodiscard]] inline auto find(const document& source, std::string_view key)
    -> const document*
{
    if (!source.is_object())
        return nullptr;
    const auto& members = source.get_object();
    const auto found = members.find(key);
    return found == members.end() ? nullptr : std::addressof(found->second);
}

/**
 * @brief Reads an object member or returns the supplied fallback.
 */
template <typename T>
[[nodiscard]] auto value_or(
    const document& source, std::string_view key, T fallback) -> T
{
    const auto* found = find(source, key);
    if (found == nullptr)
        return fallback;
    try
    {
        if constexpr (std::same_as<std::remove_cvref_t<T>, document>)
            return *found;
        else if constexpr (std::same_as<std::remove_cvref_t<T>, bool>)
            return found->is_boolean() ? found->template get<bool>()
                                       : fallback;
        else if constexpr (std::same_as<std::remove_cvref_t<T>, std::string>)
            return found->is_string()
                ? found->template get<std::string>()
                : fallback;
        else if constexpr (std::is_arithmetic_v<std::remove_cvref_t<T>>)
            return found->is_number() ? found->template as<T>() : fallback;
        else
            return found->template as<T>();
    }
    catch (...)
    {
        return fallback;
    }
}

/**
 * @brief Reads a string member or returns a copied string fallback.
 */
[[nodiscard]] inline auto value_or(
    const document& source, std::string_view key, const char* fallback)
    -> std::string
{
    return value_or(source, key, std::string{fallback});
}

/**
 * @brief Reads a string member or returns a copied string-view fallback.
 */
[[nodiscard]] inline auto value_or(
    const document& source, std::string_view key, std::string_view fallback)
    -> std::string
{
    return value_or(source, key, std::string{fallback});
}

namespace detail {

template <typename T>
concept document_mapped = requires(const T& source, document& document_source) {
    { document_codec<T>::encode(source, false) }
        -> std::same_as<std::expected<document, std::error_code>>;
    { document_codec<T>::decode(document_source, true) }
        -> std::same_as<std::expected<T, std::error_code>>;
};

struct strict_read_options : glz::opts
{
    bool error_on_unknown_keys = true;
    bool error_on_missing_keys = true;
    bool validate_trailing_whitespace = true;
};

struct lenient_read_options : glz::opts
{
    bool error_on_unknown_keys = false;
    bool error_on_missing_keys = true;
    bool validate_trailing_whitespace = true;
};

struct defaulted_read_options : glz::opts
{
    bool error_on_unknown_keys = true;
    bool error_on_missing_keys = false;
    bool validate_trailing_whitespace = true;
};

struct write_options : glz::opts
{
    bool skip_null_members = true;
};

struct explicit_null_write_options : glz::opts
{
    bool skip_null_members = false;
};

[[nodiscard]] inline auto read_error(const glz::error_ctx& error)
    -> std::error_code
{
    if (error.ec == glz::error_code::unknown_key)
        return make_error_code(errc::unknown_field);
    if (error.ec == glz::error_code::missing_key)
        return make_error_code(errc::missing_field);
    if (error.ec == glz::error_code::parse_number_failure ||
        error.ec == glz::error_code::expected_true_or_false ||
        error.ec == glz::error_code::invalid_nullable_read ||
        error.ec == glz::error_code::no_matching_variant_type)
        return make_error_code(errc::type_mismatch);
    return make_error_code(errc::parse_failed);
}

} // namespace detail

/**
 * @brief Strict direct Glaze codec used by default.
 */
struct default_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        if constexpr (detail::document_mapped<T>)
        {
            auto source = parse_document(input);
            if (!source)
                return std::unexpected(source.error());
            return detail::document_codec<T>::decode(*source, true);
        }
        else
        {
            T result{};
            const auto error = glz::read<detail::strict_read_options{}>(result, input);
            if (error)
                return std::unexpected(detail::read_error(error));
            return result;
        }
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        if constexpr (detail::document_mapped<T>)
        {
            auto document = detail::document_codec<T>::encode(value, false);
            if (!document)
                return std::unexpected(document.error());
            return write_document(*document);
        }
        else
        {
            auto result = glz::write<detail::write_options{}>(value);
            if (!result)
                return std::unexpected(make_error_code(errc::serialization_failed));
            return std::move(*result);
        }
    }
};

/**
 * @brief Direct Glaze codec that ignores unknown object keys.
 */
struct lenient_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        if constexpr (detail::document_mapped<T>)
        {
            auto source = parse_document(input);
            if (!source)
                return std::unexpected(source.error());
            return detail::document_codec<T>::decode(*source, false);
        }
        else
        {
            T result{};
            const auto error = glz::read<detail::lenient_read_options{}>(result, input);
            if (error)
                return std::unexpected(detail::read_error(error));
            return result;
        }
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        return default_codec::encode(value);
    }
};

/**
 * @brief Strict-key codec that preserves C++ defaults for omitted members.
 *
 * Intended for typed configuration binding after the document shape has been
 * validated. Unknown keys still fail recursively, including inside arrays.
 */
struct defaulted_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        if constexpr (detail::document_mapped<T>)
            return default_codec::decode<T>(input);
        else
        {
            T result{};
            const auto error =
                glz::read<detail::defaulted_read_options{}>(result, input);
            if (error)
                return std::unexpected(detail::read_error(error));
            return result;
        }
    }
};

/**
 * @brief Direct Glaze codec that writes nullable members explicitly.
 */
struct explicit_null_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        return default_codec::decode<T>(input);
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        if constexpr (detail::document_mapped<T>)
        {
            auto document = detail::document_codec<T>::encode(value, true);
            if (!document)
                return std::unexpected(document.error());
            return write_document(*document);
        }
        else
        {
            auto result = glz::write<detail::explicit_null_write_options{}>(value);
            if (!result)
                return std::unexpected(make_error_code(errc::serialization_failed));
            return std::move(*result);
        }
    }
};

/**
 * @brief Parses directly into T with the strict Glaze policy.
 */
template <typename T>
[[nodiscard]] auto parse(std::string_view input)
    -> std::expected<T, std::error_code>
{
    return default_codec::decode<T>(input);
}

/**
 * @brief Parses directly into T while allowing unknown object members.
 */
template <typename T>
[[nodiscard]] auto parse_lenient(std::string_view input)
    -> std::expected<T, std::error_code>
{
    return lenient_codec::decode<T>(input);
}

/**
 * @brief Serializes T directly with the Glaze policy.
 */
template <typename T>
[[nodiscard]] auto write(const T& value)
    -> std::expected<std::string, std::error_code>
{
    return default_codec::encode<T>(value);
}

/**
 * @brief Serializes T with explicit null members using Glaze.
 */
template <typename T>
[[nodiscard]] auto write_explicit_nulls(const T& value)
    -> std::expected<std::string, std::error_code>
{
    return explicit_null_codec::encode<T>(value);
}

/**
 * @brief Converts a value to Glaze's native dynamic document.
 */
template <typename T>
[[nodiscard]] auto to_document(const T& value, bool emit_nulls = false)
    -> std::expected<document, std::error_code>
{
    if constexpr (detail::document_mapped<T>)
        return detail::document_codec<T>::encode(value, emit_nulls);
    else
    {
        auto encoded = glz::write_json(value);
        if (!encoded)
            return std::unexpected(make_error_code(errc::serialization_failed));
        return parse_document(*encoded);
    }
}

/**
 * @brief Converts a native Glaze document into T.
 */
template <typename T>
[[nodiscard]] auto from_document(const document& source, bool reject_unknown = true)
    -> std::expected<T, std::error_code>
{
    if constexpr (detail::document_mapped<T>)
        return detail::document_codec<T>::decode(source, reject_unknown);
    else
    {
        auto encoded = write_document(source);
        if (!encoded)
            return std::unexpected(encoded.error());
        return default_codec::decode<T>(*encoded);
    }
}

/**
 * @brief Converts a document into a default-constructed T.
 *
 * Omitted members retain their C++ default member values; unknown members are
 * rejected recursively.
 */
template <typename T>
[[nodiscard]] auto from_document_with_defaults(const document& source)
    -> std::expected<T, std::error_code>
{
    auto encoded = write_document(source);
    if (!encoded)
        return std::unexpected(encoded.error());
    return defaulted_codec::decode<T>(*encoded);
}

} // namespace cnetmod::json

export template <>
struct std::is_error_code_enum<cnetmod::json::errc> : std::true_type
{
};
