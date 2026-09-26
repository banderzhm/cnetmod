module;

#include <glaze/json/generic.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

module cnetmod.json;

import std;

namespace cnetmod::json {
namespace {

class category final : public std::error_category
{
public:
    [[nodiscard]] auto name() const noexcept -> const char* override
    {
        return "cnetmod.json";
    }

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

struct document_read_options : glz::opts
{
    bool validate_trailing_whitespace = true;
};

struct pretty_write_options : glz::opts
{
    bool prettify = true;
};

} // namespace

auto make_error_code(errc value) noexcept -> std::error_code
{
    static const category instance;
    return {static_cast<int>(value), instance};
}

auto parse_document(std::string_view input)
    -> std::expected<document, std::error_code>
{
    document result;
    const auto error = glz::read<document_read_options{}>(result, input);
    if (error)
        return std::unexpected(detail::read_error(error));
    return result;
}

auto write_document(const document& value, bool prettify)
    -> std::expected<std::string, std::error_code>
{
    auto result = prettify ? glz::write<pretty_write_options{}>(value)
                           : glz::write_json(value);
    if (!result)
        return std::unexpected(make_error_code(errc::serialization_failed));
    return std::move(*result);
}

} // namespace cnetmod::json
