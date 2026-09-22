/// cnetmod.protocol.openai:chat — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :tool_contracts;
import :messages;
import :chat;
import cnetmod.json;

namespace cnetmod::openai {

namespace {
    auto string_field(const json& value, const char* key) -> std::string
    {
        if (!value.contains(key) || !value[key].is_string())
            return {};
        return value[key].get<std::string>();
    }
} // namespace

auto chat_request::set_extra_text(std::string key, std::string value) -> void
{
    extra_body[std::move(key)] = std::move(value);
}

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
    auto wire_messages = cnetmod::json::array();
    for (const auto& item : messages)
        wire_messages.get_array().push_back(item.to_json_object());
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
        value["response_format"] = cnetmod::json::object(
            {{"type", "json_schema"},
                {"json_schema", cnetmod::json::object(
                                    {{"name", response_schema_name},
                                        {"strict", response_schema_strict},
                                        {"schema", response_schema}})}});
    else if (!response_format.empty())
        value["response_format"] =
            cnetmod::json::object({{"type", response_format}});
    if (!tools.empty())
    {
        auto wire_tools = cnetmod::json::array();
        for (const auto& item : tools)
        {
            json function{{"name", item.function_name}, {"strict", item.strict}};
            if (!item.function_description.empty())
                function["description"] = item.function_description;
            if (!item.function_parameters.empty())
                function["parameters"] = item.function_parameters;
            wire_tools.get_array().push_back(cnetmod::json::object(
                {{"type", item.type}, {"function", std::move(function)}}));
        }
        value["tools"] = std::move(wire_tools);
    }
    if (!tool_choice_object.is_null())
        value["tool_choice"] = tool_choice_object;
    else if (!tool_choice.empty())
        value["tool_choice"] = tool_choice;
    if (extra_body.is_object())
        for (const auto& [key, item] : extra_body.get_object())
            value[key] = item;
    return cnetmod::json::write_document(value).value_or("{}");
}

auto chat_chunk::from_json(std::string_view text) -> chat_chunk
{
    chat_chunk result;
    auto parsed = cnetmod::json::parse_document(text);
    if (!parsed)
        return result;
    const auto& value = *parsed;
    result.id = string_field(value, "id");
    result.model = string_field(value, "model");
    if (value.contains("usage") && value["usage"].is_object())
    {
        const auto& tokens = value["usage"];
        result.token_usage = usage{
            .prompt_tokens = cnetmod::json::value_or(tokens, "prompt_tokens", 0),
            .completion_tokens = cnetmod::json::value_or(
                tokens, "completion_tokens", 0),
            .total_tokens = cnetmod::json::value_or(tokens, "total_tokens", 0)};
    }
    if (!value.contains("choices") || !value["choices"].is_array() ||
        value["choices"].get_array().empty())
        return result;
    const auto& choice = value["choices"][0];
    if (choice.contains("delta") && choice["delta"].is_object())
    {
        const auto& delta = choice["delta"];
        result.delta_role = string_field(delta, "role");
        result.delta_content = string_field(delta, "content");
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array())
        {
            for (const auto& item : delta["tool_calls"].get_array())
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
                result.delta_tool_calls.push_back({.index = cnetmod::json::value_or(
                                                       item, "index", std::size_t{0}),
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
    auto parsed_value = cnetmod::json::parse_document(text);
    if (!parsed_value)
        return result;
    const auto& value = *parsed_value;
    result.id = cnetmod::json::value_or(value, "id", std::string{});
    result.model = cnetmod::json::value_or(value, "model", std::string{});
    if (value.contains("choices") && value["choices"].is_array())
    {
        for (const auto& wire_choice : value["choices"].get_array())
        {
            choice parsed{.index = cnetmod::json::value_or(
                              wire_choice, "index", 0),
                .finish_reason = cnetmod::json::value_or(
                    wire_choice, "finish_reason", std::string{})};
            if (wire_choice.contains("message") && wire_choice["message"].is_object())
            {
                const auto& wire_message = wire_choice["message"];
                parsed.msg.role = cnetmod::json::value_or(
                    wire_message, "role", std::string{});
                parsed.msg.content = cnetmod::json::value_or(
                    wire_message, "content", std::string{});
                if (wire_message.contains("tool_calls") && wire_message["tool_calls"].is_array())
                    for (const auto& item : wire_message["tool_calls"].get_array())
                    {
                        tool_call call{.id = cnetmod::json::value_or(
                                           item, "id", std::string{}),
                            .type = cnetmod::json::value_or(
                                item, "type", std::string{"function"})};
                        if (item.contains("function") && item["function"].is_object())
                        {
                            call.function.name = cnetmod::json::value_or(
                                item["function"], "name", std::string{});
                            call.function.arguments = cnetmod::json::value_or(
                                item["function"], "arguments", std::string{});
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
        result.token_usage.prompt_tokens = cnetmod::json::value_or(
            tokens, "prompt_tokens", 0);
        result.token_usage.completion_tokens = cnetmod::json::value_or(
            tokens, "completion_tokens", 0);
        result.token_usage.total_tokens = cnetmod::json::value_or(
            tokens, "total_tokens", 0);
    }
    return result;
}

} // namespace cnetmod::openai
