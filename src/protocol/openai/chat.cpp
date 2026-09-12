/// cnetmod.protocol.openai:chat — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :tool_contracts;
import :messages;
import :chat;

namespace cnetmod::openai {

namespace {
    auto string_field(const json& value, const char* key) -> std::string
    {
        if (!value.contains(key) || !value[key].is_string())
            return {};
        return value[key].get<std::string>();
    }
} // namespace

auto chat_request::to_json() const -> std::string
{
    json value;
    value["model"] = model;
    value["temperature"] = temperature;
    if (max_completion_tokens)
        value["max_completion_tokens"] = *max_completion_tokens;
    else
        value["max_tokens"] = max_tokens;
    value["stream"] = stream;
    auto wire_messages = json::array();
    for (const auto& item : messages)
        wire_messages.push_back(item.to_json_object());
    value["messages"] = std::move(wire_messages);
    if (top_p != 1.0)
        value["top_p"] = top_p;
    if (frequency_penalty != 0.0)
        value["frequency_penalty"] = frequency_penalty;
    if (presence_penalty != 0.0)
        value["presence_penalty"] = presence_penalty;
    if (!stop.empty())
        value["stop"] = stop;
    if (n != 1)
        value["n"] = n;
    if (seed)
        value["seed"] = *seed;
    if (!user.empty())
        value["user"] = user;
    if (response_format == "json_schema")
        value["response_format"] = {{"type", "json_schema"},
            {"json_schema", {{"name", response_schema_name}, {"strict", response_schema_strict}, {"schema", response_schema}}}};
    else if (!response_format.empty())
        value["response_format"] = {{"type", response_format}};
    if (!tools.empty())
    {
        auto wire_tools = json::array();
        for (const auto& item : tools)
        {
            json function{{"name", item.function_name}, {"strict", item.strict}};
            if (!item.function_description.empty())
                function["description"] = item.function_description;
            if (!item.function_parameters.empty())
                function["parameters"] = item.function_parameters;
            wire_tools.push_back({{"type", item.type},
                {"function", std::move(function)}});
        }
        value["tools"] = std::move(wire_tools);
    }
    if (!tool_choice_object.is_null())
        value["tool_choice"] = tool_choice_object;
    else if (!tool_choice.empty())
        value["tool_choice"] = tool_choice;
    for (auto item = extra_body.begin(); item != extra_body.end(); ++item)
        value[item.key()] = item.value();
    return value.dump();
}

auto chat_chunk::from_json(std::string_view text) -> chat_chunk
{
    chat_chunk result;
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
        return result;
    result.id = string_field(value, "id");
    result.model = string_field(value, "model");
    if (value.contains("usage") && value["usage"].is_object())
    {
        const auto& tokens = value["usage"];
        result.token_usage = usage{
            .prompt_tokens = tokens.value("prompt_tokens", 0),
            .completion_tokens = tokens.value("completion_tokens", 0),
            .total_tokens = tokens.value("total_tokens", 0)};
    }
    if (!value.contains("choices") || !value["choices"].is_array() ||
        value["choices"].empty())
        return result;
    const auto& choice = value["choices"][0];
    if (choice.contains("delta") && choice["delta"].is_object())
    {
        const auto& delta = choice["delta"];
        result.delta_role = string_field(delta, "role");
        result.delta_content = string_field(delta, "content");
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array())
        {
            for (const auto& item : delta["tool_calls"])
            {
                tool_call call{.id = string_field(item, "id"),
                    .type = string_field(item, "type")};
                if (call.type.empty())
                    call.type = "function";
                if (item.contains("function") && item["function"].is_object())
                {
                    call.function.name = string_field(item["function"], "name");
                    call.function.arguments = string_field(item["function"], "arguments");
                }
                result.delta_tool_calls.push_back({.index = item.value("index", std::size_t{0}),
                    .value = std::move(call)});
            }
        }
    }
    result.finish_reason = string_field(choice, "finish_reason");
    return result;
}

auto chat_response::content() const -> std::string_view
{
    return choices.empty() ? std::string_view{} : std::string_view{choices.front().msg.content};
}

auto chat_response::from_json(std::string_view text) -> chat_response
{
    chat_response result;
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
        return result;
    result.id = value.value("id", "");
    result.model = value.value("model", "");
    if (value.contains("choices") && value["choices"].is_array())
    {
        for (const auto& wire_choice : value["choices"])
        {
            choice parsed{.index = wire_choice.value("index", 0),
                .finish_reason = wire_choice.value("finish_reason", "")};
            if (wire_choice.contains("message") && wire_choice["message"].is_object())
            {
                const auto& wire_message = wire_choice["message"];
                parsed.msg.role = wire_message.value("role", "");
                parsed.msg.content = wire_message.value("content", "");
                if (wire_message.contains("tool_calls") && wire_message["tool_calls"].is_array())
                    for (const auto& item : wire_message["tool_calls"])
                    {
                        tool_call call{.id = item.value("id", ""),
                            .type = item.value("type", "function")};
                        if (item.contains("function") && item["function"].is_object())
                        {
                            call.function.name = item["function"].value("name", "");
                            call.function.arguments = item["function"].value("arguments", "");
                        }
                        parsed.msg.tool_calls.push_back(std::move(call));
                    }
            }
            result.choices.push_back(std::move(parsed));
        }
    }
    if (value.contains("usage") && value["usage"].is_object())
    {
        const auto& tokens = value["usage"];
        result.token_usage.prompt_tokens = tokens.value("prompt_tokens", 0);
        result.token_usage.completion_tokens = tokens.value("completion_tokens", 0);
        result.token_usage.total_tokens = tokens.value("total_tokens", 0);
    }
    return result;
}

} // namespace cnetmod::openai
