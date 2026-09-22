/// cnetmod.protocol.openai:prompt — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :messages;
import :prompt;
import cnetmod.json;

namespace cnetmod::openai {

prompt_section::prompt_section() = default;

prompt_section::prompt_section(prompt_variables variables)
    : variables(std::move(variables))
{
}

auto prompt_section::with_sections(prompt_sections nested) && -> prompt_section
{
    sections = std::move(nested);
    return std::move(*this);
}

auto string_output_parser::parse(std::string_view text) const
    -> std::expected<json, std::string>
{
    return json(std::string(text));
}

json_output_parser::json_output_parser(json schema) : schema_(std::move(schema)) {}

auto json_output_parser::parse(std::string_view text) const
    -> std::expected<json, std::string>
{
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
        return std::unexpected("model output is not valid JSON");
    if (!schema_.empty())
    {
        auto valid = validate_json_schema(value, schema_);
        if (!valid)
            return std::unexpected(valid.error());
    }
    return value;
}

auto validate_json_schema(const json& value, const json& schema,
    std::string_view path) -> std::expected<void, std::string>
{
    if (!schema.is_object())
        return std::unexpected(std::format("{}: schema must be an object", path));
    if (schema.contains("enum") && schema["enum"].is_array())
    {
        auto matched = false;
        for (const auto& candidate : schema["enum"])
        {
            if (candidate == value)
            {
                matched = true;
                break;
            }
        }
        if (!matched)
            return std::unexpected(std::format("{}: value is not in enum", path));
    }

    const auto type = schema.value("type", "");
    const bool type_matches = type.empty() ||
        (type == "object" && value.is_object()) ||
        (type == "array" && value.is_array()) ||
        (type == "string" && value.is_string()) ||
        (type == "integer" && value.is_number_integer()) ||
        (type == "number" && value.is_number()) ||
        (type == "boolean" && value.is_boolean()) ||
        (type == "null" && value.is_null());
    if (!type_matches)
        return std::unexpected(std::format("{}: expected type {}", path, type));

    if (value.is_object())
    {
        if (schema.contains("required") && schema["required"].is_array())
        {
            for (const auto& required : schema["required"])
            {
                if (required.is_string() && !value.contains(required.get<std::string>()))
                    return std::unexpected(std::format("{}: missing required property {}",
                        path, required.get<std::string>()));
            }
        }
        const auto has_properties = schema.contains("properties") &&
            schema["properties"].is_object();
        for (auto entry = value.begin(); entry != value.end(); ++entry)
        {
            if (has_properties && schema["properties"].contains(entry.key()))
            {
                auto nested = validate_json_schema(entry.value(),
                    schema["properties"][entry.key()],
                    std::format("{}.{}", path, entry.key()));
                if (!nested)
                    return nested;
            }
            else if (schema.value("additionalProperties", true) == false)
                return std::unexpected(std::format("{}: unexpected property {}",
                    path, entry.key()));
        }
    }
    if (value.is_array() && schema.contains("items"))
    {
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            auto nested = validate_json_schema(value[index], schema["items"],
                std::format("{}[{}]", path, index));
            if (!nested)
                return nested;
        }
    }
    return {};
}

} // namespace cnetmod::openai
