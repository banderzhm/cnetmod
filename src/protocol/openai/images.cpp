/// cnetmod.protocol.openai:images — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :images;
import cnetmod.json;

namespace cnetmod::openai {

auto image_generation_request::to_json() const -> std::string
{
    json value{{"model", model}, {"prompt", prompt}};
    if (n != 1)
        value["n"] = n;
    if (quality != "standard")
        value["quality"] = quality;
    if (response_format != "url")
        value["response_format"] = response_format;
    if (size != "1024x1024")
        value["size"] = size;
    if (style != "vivid")
        value["style"] = style;
    if (!user.empty())
        value["user"] = user;
    return cnetmod::json::write_document(value).value_or("{}");
}

auto image_response::from_json(std::string_view text) -> image_response
{
    image_response result;
    auto parsed = cnetmod::json::parse_document(text);
    if (!parsed)
        return result;
    const auto& value = *parsed;
    result.created = cnetmod::json::value_or(
        value, "created", std::int64_t{0});
    if (value.contains("data") && value["data"].is_array())
        for (const auto& item : value["data"].get_array())
            result.data.push_back({.url = cnetmod::json::value_or(
                                       item, "url", std::string{}),
                .b64_json = cnetmod::json::value_or(
                    item, "b64_json", std::string{}),
                .revised_prompt = cnetmod::json::value_or(
                    item, "revised_prompt", std::string{})});
    return result;
}

} // namespace cnetmod::openai
