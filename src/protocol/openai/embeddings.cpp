/// cnetmod.protocol.openai:embeddings — implementations

module;

#include <cnetmod/config.hpp>
#include "../../utils/detail/simdjson_depth.hpp"

module cnetmod.protocol.openai;

import std;
import :foundation;
import :embeddings;

namespace cnetmod::openai {

auto embedding_request::to_json() const -> std::string
{
    simdjson::builder::string_builder builder;
    builder.start_object();
    if (dimensions)
    {
        builder.append_key_value("dimensions", *dimensions);
        builder.append_comma();
    }
    if (!encoding_format.empty())
    {
        builder.append_key_value("encoding_format", std::string_view{encoding_format});
        builder.append_comma();
    }
    if (input.size() == 1)
        builder.append_key_value("input", std::string_view{input.front()});
    else
        builder.append_key_value("input", std::span<const std::string>{input});
    builder.append_comma();
    builder.append_key_value("model", std::string_view{model});
    if (!user.empty())
    {
        builder.append_comma();
        builder.append_key_value("user", std::string_view{user});
    }
    builder.end_object();
    std::string_view output;
    if (builder.view().get(output) || !builder.validate_unicode())
        return "{}";
    return std::string{output};
}

auto embedding_response::from_json(std::string_view text) -> embedding_response
{
    using cnetmod::detail::find_last_json_field;
    simdjson::dom::parser parser;
    simdjson::dom::element value;
    if (!cnetmod::detail::parse_bounded_json(parser, text, value))
        return {};

    const auto read_int = [](simdjson::dom::element object, std::string_view key) {
        std::int64_t field{};
        return !find_last_json_field(object, key).get_int64().get(field) &&
                   std::in_range<int>(field)
            ? static_cast<int>(field)
            : 0;
    };

    embedding_response result;
    std::string_view model;
    if (!find_last_json_field(value, "model").get_string().get(model))
        result.model = model;
    simdjson::dom::array data;
    if (!find_last_json_field(value, "data").get_array().get(data))
    {
        result.data.reserve(data.size());
        for (const auto item : data)
        {
            auto& embedding = result.data.emplace_back();
            embedding.index = read_int(item, "index");
            simdjson::dom::array components;
            if (find_last_json_field(item, "embedding").get_array().get(components))
                continue;
            embedding.embedding.reserve(components.size());
            for (const auto component : components)
            {
                double number{};
                if (component.get_double().get(number) ||
                    number > std::numeric_limits<float>::max() ||
                    number < -std::numeric_limits<float>::max())
                    return {};
                embedding.embedding.push_back(static_cast<float>(number));
            }
        }
    }
    simdjson::dom::element tokens;
    if (!find_last_json_field(value, "usage").get(tokens))
    {
        result.token_usage.prompt_tokens = read_int(tokens, "prompt_tokens");
        result.token_usage.total_tokens = read_int(tokens, "total_tokens");
    }
    return result;
}

} // namespace cnetmod::openai
