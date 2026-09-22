/// cnetmod.protocol.openai:messages — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :tool_contracts;
import :messages;
import cnetmod.json;

namespace cnetmod::openai {

auto content_part::make_text(std::string_view text) -> content_part
{
    return content_part{.type = "text", .text = std::string(text), .image_url = {}};
}

auto content_part::make_image_url(std::string_view url, std::string_view detail)
    -> content_part
{
    return content_part{.type = "image_url", .text = {}, .image_url = {.url = std::string(url), .detail = std::string(detail)}};
}

auto content_part::make_image_base64(std::string_view base64_data,
    std::string_view media_type, std::string_view detail) -> content_part
{
    return make_image_url(std::format("data:{};base64,{}", media_type, base64_data),
        detail);
}

auto content_part::to_json_object() const -> json
{
    json value;
    value["type"] = type;
    if (type == "text")
        value["text"] = text;
    else if (type == "image_url")
    {
        value["image_url"] =
            cnetmod::json::object({{"url", image_url.url}});
        if (image_url.detail != "auto")
            value["image_url"]["detail"] = image_url.detail;
    }
    return value;
}

auto message::user(std::string_view text) -> message
{
    return message{.role = "user", .content = std::string(text)};
}

auto message::system(std::string_view text) -> message
{
    return message{.role = "system", .content = std::string(text)};
}

auto message::model_output(std::string_view text) -> message
{
    return message{.role = "assistant", .content = std::string(text)};
}

auto message::developer(std::string_view text) -> message
{
    return message{.role = "developer", .content = std::string(text)};
}

auto message::tool_result(std::string_view call_id, std::string_view text,
    std::string_view tool_name) -> message
{
    return message{.role = "tool", .content = std::string(text), .name = std::string(tool_name), .tool_call_id = std::string(call_id)};
}

auto message::tool_call_request(std::vector<tool_call> calls,
    std::string_view text) -> message
{
    return message{.role = "assistant", .content = std::string(text), .tool_calls = std::move(calls)};
}

auto message::user_multimodal(std::vector<content_part> parts) -> message
{
    return message{.role = "user", .content_parts = std::move(parts)};
}

auto message::from_json_object(const json& value)
    -> std::expected<message, std::string>
{
    if (!value.is_object())
        return std::unexpected("message must be a JSON object");
    if (!value.contains("role") || !value["role"].is_string())
        return std::unexpected("message role must be a string");
    message result;
    result.role = value["role"].get<std::string>();
    if (value.contains("content"))
    {
        if (value["content"].is_string())
            result.content = value["content"].get<std::string>();
        else if (value["content"].is_array())
        {
            for (const auto& part : value["content"].get_array())
            {
                if (!part.is_object() || !part.contains("type") ||
                    !part["type"].is_string())
                    return std::unexpected(
                        "message content part has no string type");
                content_part decoded;
                decoded.type = part["type"].get<std::string>();
                if (decoded.type == "text")
                {
                    if (!part.contains("text") || !part["text"].is_string())
                        return std::unexpected(
                            "text content part has no string text");
                    decoded.text = part["text"].get<std::string>();
                }
                else if (decoded.type == "image_url")
                {
                    if (!part.contains("image_url") ||
                        !part["image_url"].is_object() ||
                        !part["image_url"].contains("url") ||
                        !part["image_url"]["url"].is_string())
                        return std::unexpected(
                            "image content part has no URL");
                    decoded.image_url.url =
                        part["image_url"]["url"].get<std::string>();
                    decoded.image_url.detail = cnetmod::json::value_or(
                        part["image_url"], "detail", std::string{"auto"});
                }
                else
                {
                    return std::unexpected(
                        "unsupported message content part: " + decoded.type);
                }
                result.content_parts.push_back(std::move(decoded));
            }
        }
        else if (!value["content"].is_null())
        {
            return std::unexpected(
                "message content must be a string, array or null");
        }
    }
    result.name = cnetmod::json::value_or(value, "name", std::string{});
    result.tool_call_id = cnetmod::json::value_or(
        value, "tool_call_id", std::string{});
    if (value.contains("tool_calls"))
    {
        if (!value["tool_calls"].is_array())
            return std::unexpected("message tool_calls must be an array");
        for (const auto& call : value["tool_calls"].get_array())
        {
            if (!call.is_object() || !call.contains("function") ||
                !call["function"].is_object())
                return std::unexpected("invalid message tool call");
            result.tool_calls.push_back({.id = cnetmod::json::value_or(
                                             call, "id", std::string{}),
                .type = cnetmod::json::value_or(
                    call, "type", std::string{"function"}),
                .function = {
                    .name = cnetmod::json::value_or(
                        call["function"], "name", std::string{}),
                    .arguments = cnetmod::json::value_or(
                        call["function"], "arguments", std::string{})}});
        }
    }
    return result;
}

auto message::to_json_object() const -> json
{
    json value;
    value["role"] = role;
    if (!content_parts.empty())
    {
        auto content = cnetmod::json::array();
        for (const auto& part : content_parts)
            content.get_array().push_back(part.to_json_object());
        value["content"] = std::move(content);
    }
    else
    {
        value["content"] = content;
    }
    if (!name.empty())
        value["name"] = name;
    if (!tool_call_id.empty())
        value["tool_call_id"] = tool_call_id;
    if (!tool_calls.empty())
    {
        auto calls = cnetmod::json::array();
        for (const auto& call : tool_calls)
            calls.get_array().push_back(cnetmod::json::object(
                {{"id", call.id},
                    {"type", call.type},
                    {"function", cnetmod::json::object(
                                     {{"name", call.function.name},
                                         {"arguments", call.function.arguments}})}}));
        value["tool_calls"] = std::move(calls);
    }
    return value;
}

} // namespace cnetmod::openai
