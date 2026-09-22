module;

#include <glaze/json/generic.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

module cnetmod.json;

import std;

namespace cnetmod::json {
namespace {
    struct document_read_options : glz::opts
    {
        bool validate_trailing_whitespace = true;
    };

    class category final : public std::error_category
    {
    public:
        [[nodiscard]] auto name() const noexcept -> const char* override { return "cnetmod.json"; }
        [[nodiscard]] auto message(int value) const -> std::string override
        {
            switch (static_cast<errc>(value))
            {
            case errc::parse_failed: return "JSON parsing failed";
            case errc::serialization_failed: return "JSON serialization failed";
            case errc::type_mismatch: return "JSON value has an incompatible type";
            case errc::missing_field: return "JSON document is missing a required field";
            case errc::unknown_field: return "JSON document contains an unknown field";
            }
            return "unknown JSON error";
        }
    };

    auto from_glaze(const glz::generic_u64& source) -> document
    {
        if (source.is_null()) return nullptr;
        if (source.is_boolean()) return source.get<bool>();
        if (source.is_string()) return source.get<std::string>();
        if (source.is_uint64()) return source.get<std::uint64_t>();
        if (source.is_int64()) return source.get<std::int64_t>();
        if (source.is_double()) return source.get<double>();
        if (source.is_array())
        {
            auto result = document::array();
            for (const auto& entry : source.get_array()) result.push_back(from_glaze(entry));
            return result;
        }
        auto result = document::object();
        for (const auto& [key, entry] : source.get_object()) result[key] = from_glaze(entry);
        return result;
    }

    auto to_glaze(const document& source) -> glz::generic_u64
    {
        if (source.is_null()) return nullptr;
        if (source.is_boolean()) return source.get<bool>();
        if (source.is_string()) return source.get<std::string>();
        if (source.is_number_unsigned()) return source.get<std::uint64_t>();
        if (source.is_number_integer()) return source.get<std::int64_t>();
        if (source.is_number_float()) return source.get<double>();
        if (source.is_array())
        {
            glz::generic_u64 result(glz::generic_u64::array_t{});
            auto& values = result.get_array();
            values.reserve(source.size());
            for (const auto& entry : source) values.push_back(to_glaze(entry));
            return result;
        }
        glz::generic_u64 result(glz::generic_u64::object_t{});
        for (const auto& [key, entry] :
            source.get_ref<const document::object_type&>())
            result[key] = to_glaze(entry);
        return result;
    }
} // namespace

auto make_error_code(errc value) noexcept -> std::error_code
{
    static const category instance;
    return {static_cast<int>(value), instance};
}

auto parse_document(std::string_view input) -> std::expected<document, std::error_code>
{
    glz::generic_u64 parsed;
    const auto error = glz::read<document_read_options{}>(parsed, input);
    if (error) return std::unexpected(make_error_code(errc::parse_failed));
    return from_glaze(parsed);
}

auto write_document(const document& value) -> std::expected<std::string, std::error_code>
{
    auto encoded = glz::write_json(to_glaze(value));
    if (!encoded) return std::unexpected(make_error_code(errc::serialization_failed));
    return std::move(*encoded);
}

auto document::parse(std::string_view input, std::nullptr_t,
    bool allow_exceptions, bool) -> document
{
    auto parsed = parse_document(input);
    if (parsed)
        return std::move(*parsed);
    if (allow_exceptions)
        throw exception{"invalid JSON document"};
    document result;
    result.discarded_ = true;
    return result;
}

auto document::parse(std::istream& input, std::nullptr_t callback,
    bool allow_exceptions, bool ignore_comments) -> document
{
    return parse(std::string{std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}}, callback, allow_exceptions,
        ignore_comments);
}

auto document::dump(int) const -> std::string
{
    auto encoded = write_document(*this);
    if (!encoded)
        throw exception{"failed to serialize JSON document"};
    return std::move(*encoded);
}

} // namespace cnetmod::json
