/// cnetmod.protocol.openai:embeddings — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :embeddings;
import cnetmod.json;

namespace cnetmod::openai {

auto embedding_request::to_json() const -> std::string
{
    json value;
    value["model"] = model;
    if (input.size() == 1)
        value["input"] = input.front();
    else
    {
        auto inputs = cnetmod::json::array();
        for (const auto& item : input)
            inputs.get_array().emplace_back(item);
        value["input"] = std::move(inputs);
    }
    if (!encoding_format.empty())
        value["encoding_format"] = encoding_format;
    if (dimensions)
        value["dimensions"] = *dimensions;
    if (!user.empty())
        value["user"] = user;
    return cnetmod::json::write_document(value).value_or("{}");
}

auto embedding_response::from_json(std::string_view text) -> embedding_response
{
    embedding_response result;
    auto parsed = cnetmod::json::parse_document(text);
    if (!parsed)
        return result;
    const auto& value = *parsed;
    result.model = cnetmod::json::value_or(value, "model", std::string{});
    if (value.contains("data") && value["data"].is_array())
    {
        for (const auto& item : value["data"].get_array())
        {
            embedding_data embedding{.index = cnetmod::json::value_or(
                                         item, "index", 0)};
            if (item.contains("embedding") && item["embedding"].is_array())
                for (const auto& component : item["embedding"].get_array())
                    embedding.embedding.push_back(component.as<float>());
            result.data.push_back(std::move(embedding));
        }
    }
    if (value.contains("usage") && value["usage"].is_object())
    {
        const auto& tokens = value["usage"];
        result.token_usage.prompt_tokens = cnetmod::json::value_or(
            tokens, "prompt_tokens", 0);
        result.token_usage.total_tokens = cnetmod::json::value_or(
            tokens, "total_tokens", 0);
    }
    return result;
}

} // namespace cnetmod::openai
