/// cnetmod.protocol.openai:prompt — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :messages;
import :prompt;
import nlohmann.json;

namespace cnetmod::openai {

prompt_template::prompt_template(std::string source) : source_(std::move(source)) {}

auto prompt_template::format(const prompt_variables& variables) const
    -> std::expected<std::string, std::string>
{
    std::string output;
    output.reserve(source_.size());
    for (std::size_t index = 0; index < source_.size();)
    {
        if (source_.substr(index, 2) == "{{")
        {
            output.push_back('{');
            index += 2;
            continue;
        }
        if (source_.substr(index, 2) == "}}")
        {
            output.push_back('}');
            index += 2;
            continue;
        }
        if (source_[index] != '{')
        {
            output.push_back(source_[index++]);
            continue;
        }
        const auto end = source_.find('}', index + 1);
        if (end == std::string::npos)
            return std::unexpected(std::format("unclosed prompt variable at byte {}", index));
        const auto name = source_.substr(index + 1, end - index - 1);
        if (name.empty())
            return std::unexpected("empty prompt variable");
        const auto found = variables.find(name);
        if (found == variables.end())
            return std::unexpected("missing prompt variable: " + name);
        output += found->second;
        index = end + 1;
    }
    return output;
}

auto prompt_template::source() const noexcept -> std::string_view
{
    return source_;
}

auto prompt_template::variables() const -> std::vector<std::string>
{
    std::vector<std::string> result;
    for (std::size_t index = 0; index < source_.size();)
    {
        if (source_.substr(index, 2) == "{{")
        {
            index += 2;
            continue;
        }
        if (source_[index] != '{')
        {
            ++index;
            continue;
        }
        const auto end = source_.find('}', index + 1);
        if (end == std::string::npos)
            break;
        auto name = source_.substr(index + 1, end - index - 1);
        if (!name.empty() && std::ranges::find(result, name) == result.end())
            result.push_back(std::move(name));
        index = end + 1;
    }
    return result;
}

chat_prompt_template::chat_prompt_template(std::vector<message_prompt> prompts)
    : prompts_(std::move(prompts))
{
}

void chat_prompt_template::add(std::string role, prompt_template prompt)
{
    prompts_.push_back({std::move(role), std::move(prompt)});
}

auto chat_prompt_template::format(const prompt_variables& variables) const
    -> std::expected<std::vector<message>, std::string>
{
    std::vector<message> result;
    result.reserve(prompts_.size());
    for (const auto& item : prompts_)
    {
        auto content = item.prompt.format(variables);
        if (!content)
            return std::unexpected(content.error());
        result.push_back(message{.role = item.role, .content = std::move(*content)});
    }
    return result;
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
