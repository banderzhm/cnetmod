/// cnetmod.protocol.openai:responses — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :tool_contracts;
import :messages;
import :responses;
import cnetmod.json;

namespace cnetmod::openai {

auto response_request::to_json() const -> std::string
{
    json value{{"model", model}};
    if (!instructions.empty())
        value["instructions"] = instructions;
    auto input_items = cnetmod::json::array();
    for (const auto& item : input)
    {
        if (item.content_parts.empty())
        {
            input_items.get_array().push_back(item.to_json_object());
            continue;
        }
        auto content = cnetmod::json::array();
        for (const auto& part : item.content_parts)
        {
            if (part.type == "text")
                content.get_array().push_back(cnetmod::json::object(
                    {{"type", "input_text"}, {"text", part.text}}));
            else if (part.type == "image_url")
                content.get_array().push_back(cnetmod::json::object(
                    {{"type", "input_image"},
                        {"image_url", part.image_url.url},
                        {"detail", part.image_url.detail}}));
        }
        input_items.get_array().push_back(cnetmod::json::object(
            {{"role", item.role}, {"content", std::move(content)}}));
    }
    for (const auto& item : tool_outputs)
        input_items.get_array().push_back(cnetmod::json::object(
            {{"type", "function_call_output"},
                {"call_id", item.call_id}, {"output", item.output}}));
    for (const auto& item : additional_input_items)
        input_items.get_array().push_back(item);
    value["input"] = std::move(input_items);

    auto wire_tools = cnetmod::json::array();
    for (const auto& item : tools)
        wire_tools.get_array().push_back(cnetmod::json::object({{"type", "function"},
            {"name", item.function_name},
            {"description", item.function_description},
            {"parameters", item.function_parameters},
            {"strict", item.strict}}));
    for (const auto& item : additional_tools)
        wire_tools.get_array().push_back(item);
    if (!wire_tools.get_array().empty())
        value["tools"] = std::move(wire_tools);
    if (!tool_choice_object.is_null())
        value["tool_choice"] = tool_choice_object;
    else if (!tool_choice.empty())
        value["tool_choice"] = tool_choice;
    if (!previous_response_id.empty())
        value["previous_response_id"] = previous_response_id;
    if (max_output_tokens)
        value["max_output_tokens"] = *max_output_tokens;
    if (max_tool_calls)
        value["max_tool_calls"] = *max_tool_calls;
    if (temperature)
        value["temperature"] = *temperature;
    value["parallel_tool_calls"] = parallel_tool_calls;
    value["store"] = store;
    if (!response_schema.empty())
        value["text"]["format"] = cnetmod::json::object({{"type", "json_schema"},
            {"name", response_schema_name}, {"strict", response_schema_strict},
            {"schema", response_schema}});
    if (!metadata.empty())
    {
        auto wire_metadata = cnetmod::json::object();
        for (const auto& [key, item] : metadata)
            wire_metadata[key] = item;
        value["metadata"] = std::move(wire_metadata);
    }
    if (!service_tier.empty())
        value["service_tier"] = service_tier;
    if (!prompt_cache_key.empty())
        value["prompt_cache_key"] = prompt_cache_key;
    if (!safety_identifier.empty())
        value["safety_identifier"] = safety_identifier;
    if (!reasoning.empty())
        value["reasoning"] = reasoning;
    if (extra_body.is_object())
        for (const auto& [key, item] : extra_body.get_object())
            value[key] = item;
    return cnetmod::json::write_document(value).value_or("{}");
}

auto response_result::from_json(std::string_view text) -> response_result
{
    response_result result;
    auto parsed = cnetmod::json::parse_document(text);
    if (!parsed)
        return result;
    const auto& value = *parsed;
    result.raw = value;
    result.id = cnetmod::json::value_or(value, "id", std::string{});
    result.model = cnetmod::json::value_or(value, "model", std::string{});
    result.status = cnetmod::json::value_or(value, "status", std::string{});
    if (value.contains("output_text") && value["output_text"].is_string())
        result.output_text = value["output_text"].get<std::string>();
    const auto collect_text = result.output_text.empty();
    if (value.contains("output") && value["output"].is_array())
        for (const auto& item : value["output"].get_array())
        {
            const auto type = cnetmod::json::value_or(item, "type", std::string{});
            if (type == "function_call")
                result.tool_calls.push_back({.id = cnetmod::json::value_or(
                                                 item, "call_id",
                                                 cnetmod::json::value_or(item, "id", std::string{})),
                    .type = "function",
                    .function = {.name = cnetmod::json::value_or(item, "name", std::string{}),
                        .arguments = cnetmod::json::value_or(item, "arguments", std::string{})}});
            if (collect_text && type == "message" && item.contains("content") &&
                item["content"].is_array())
                for (const auto& content : item["content"].get_array())
                    if (cnetmod::json::value_or(content, "type", std::string{}) == "output_text")
                        result.output_text += cnetmod::json::value_or(content, "text", std::string{});
        }
    if (value.contains("usage") && value["usage"].is_object())
    {
        const auto& tokens = value["usage"];
        result.token_usage.prompt_tokens = cnetmod::json::value_or(tokens, "input_tokens", 0);
        result.token_usage.completion_tokens = cnetmod::json::value_or(tokens, "output_tokens", 0);
        result.token_usage.total_tokens = cnetmod::json::value_or(tokens, "total_tokens", 0);
    }
    return result;
}

} // namespace cnetmod::openai
