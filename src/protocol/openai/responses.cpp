/// cnetmod.protocol.openai:responses — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :tool_contracts;
import :messages;
import :responses;

namespace cnetmod::openai {

auto response_request::to_json() const -> std::string
{
    json value{{"model", model}};
    if (!instructions.empty())
        value["instructions"] = instructions;
    auto input_items = json::array();
    for (const auto& item : input)
    {
        if (item.content_parts.empty())
        {
            input_items.push_back(item.to_json_object());
            continue;
        }
        auto content = json::array();
        for (const auto& part : item.content_parts)
        {
            if (part.type == "text")
                content.push_back({{"type", "input_text"}, {"text", part.text}});
            else if (part.type == "image_url")
                content.push_back({{"type", "input_image"},
                    {"image_url", part.image_url.url},
                    {"detail", part.image_url.detail}});
        }
        input_items.push_back({{"role", item.role}, {"content", std::move(content)}});
    }
    for (const auto& item : tool_outputs)
        input_items.push_back({{"type", "function_call_output"},
            {"call_id", item.call_id}, {"output", item.output}});
    for (const auto& item : additional_input_items)
        input_items.push_back(item);
    value["input"] = std::move(input_items);

    auto wire_tools = json::array();
    for (const auto& item : tools)
        wire_tools.push_back({{"type", "function"},
            {"name", item.function_name},
            {"description", item.function_description},
            {"parameters", item.function_parameters},
            {"strict", item.strict}});
    for (const auto& item : additional_tools)
        wire_tools.push_back(item);
    if (!wire_tools.empty())
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
        value["text"]["format"] = {{"type", "json_schema"},
            {"name", response_schema_name}, {"strict", response_schema_strict},
            {"schema", response_schema}};
    if (!metadata.empty())
        value["metadata"] = metadata;
    if (!service_tier.empty())
        value["service_tier"] = service_tier;
    if (!prompt_cache_key.empty())
        value["prompt_cache_key"] = prompt_cache_key;
    if (!safety_identifier.empty())
        value["safety_identifier"] = safety_identifier;
    if (!reasoning.empty())
        value["reasoning"] = reasoning;
    for (auto item = extra_body.begin(); item != extra_body.end(); ++item)
        value[item.key()] = item.value();
    return value.dump();
}

auto response_result::from_json(std::string_view text) -> response_result
{
    response_result result;
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
        return result;
    result.raw = value;
    result.id = value.value("id", "");
    result.model = value.value("model", "");
    result.status = value.value("status", "");
    if (value.contains("output_text") && value["output_text"].is_string())
        result.output_text = value["output_text"].get<std::string>();
    const auto collect_text = result.output_text.empty();
    if (value.contains("output") && value["output"].is_array())
        for (const auto& item : value["output"])
        {
            const auto type = item.value("type", "");
            if (type == "function_call")
                result.tool_calls.push_back({.id = item.value("call_id", item.value("id", "")),
                    .type = "function",
                    .function = {.name = item.value("name", ""),
                        .arguments = item.value("arguments", "")}});
            if (collect_text && type == "message" && item.contains("content") &&
                item["content"].is_array())
                for (const auto& content : item["content"])
                    if (content.value("type", "") == "output_text")
                        result.output_text += content.value("text", "");
        }
    if (value.contains("usage") && value["usage"].is_object())
    {
        const auto& tokens = value["usage"];
        result.token_usage.prompt_tokens = tokens.value("input_tokens", 0);
        result.token_usage.completion_tokens = tokens.value("output_tokens", 0);
        result.token_usage.total_tokens = tokens.value("total_tokens", 0);
    }
    return result;
}

} // namespace cnetmod::openai
