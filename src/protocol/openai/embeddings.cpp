/// cnetmod.protocol.openai:embeddings — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :embeddings;

namespace cnetmod::openai {

auto embedding_request::to_json() const -> std::string
{
    json value;
    value["model"] = model;
    value["input"] = input.size() == 1 ? json(input.front()) : json(input);
    if (!encoding_format.empty())
        value["encoding_format"] = encoding_format;
    if (dimensions)
        value["dimensions"] = *dimensions;
    if (!user.empty())
        value["user"] = user;
    return value.dump();
}

auto embedding_response::from_json(std::string_view text) -> embedding_response
{
    embedding_response result;
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
        return result;
    result.model = value.value("model", "");
    if (value.contains("data") && value["data"].is_array())
    {
        for (const auto& item : value["data"])
        {
            embedding_data embedding{.index = item.value("index", 0)};
            if (item.contains("embedding") && item["embedding"].is_array())
                for (const auto& component : item["embedding"])
                    embedding.embedding.push_back(component.get<float>());
            result.data.push_back(std::move(embedding));
        }
    }
    if (value.contains("usage") && value["usage"].is_object())
    {
        const auto& tokens = value["usage"];
        result.token_usage.prompt_tokens = tokens.value("prompt_tokens", 0);
        result.token_usage.total_tokens = tokens.value("total_tokens", 0);
    }
    return result;
}

} // namespace cnetmod::openai
