/// cnetmod.protocol.openai:images — implementations

module;

#include <cnetmod/config.hpp>
#include "../../utils/detail/simdjson_depth.hpp"

module cnetmod.protocol.openai;

import std;
import :images;

namespace cnetmod::openai {

auto image_generation_request::to_json() const -> std::string
{
    simdjson::builder::string_builder builder;
    builder.start_object();
    builder.append_key_value("model", std::string_view{model});
    const auto append = [&](std::string_view key, auto value) {
        builder.append_comma();
        builder.append_key_value(key, value);
    };
    if (n != 1)
        append("n", n);
    append("prompt", std::string_view{prompt});
    if (quality != "standard")
        append("quality", std::string_view{quality});
    if (response_format != "url")
        append("response_format", std::string_view{response_format});
    if (size != "1024x1024")
        append("size", std::string_view{size});
    if (style != "vivid")
        append("style", std::string_view{style});
    if (!user.empty())
        append("user", std::string_view{user});
    builder.end_object();
    std::string_view output;
    if (builder.view().get(output) || !builder.validate_unicode())
        return "{}";
    return std::string{output};
}

auto image_response::from_json(std::string_view text) -> image_response
{
    using cnetmod::detail::find_last_json_field;
    simdjson::dom::parser parser;
    simdjson::dom::element value;
    if (!cnetmod::detail::parse_bounded_json(parser, text, value))
        return {};

    image_response result;
    (void)find_last_json_field(value, "created").get_int64().get(result.created);
    simdjson::dom::array data;
    if (!find_last_json_field(value, "data").get_array().get(data))
    {
        result.data.reserve(data.size());
        for (const auto item : data)
        {
            const auto read = [&](std::string_view key) -> std::string {
                std::string_view field;
                (void)find_last_json_field(item, key).get_string().get(field);
                return std::string{field};
            };
            result.data.push_back({.url = read("url"),
                .b64_json = read("b64_json"), .revised_prompt = read("revised_prompt")});
        }
    }
    return result;
}

} // namespace cnetmod::openai
