/// cnetmod.protocol.openai:images — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :images;

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
    return value.dump();
}

auto image_response::from_json(std::string_view text) -> image_response
{
    image_response result;
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
        return result;
    result.created = value.value("created", std::int64_t{0});
    if (value.contains("data") && value["data"].is_array())
        for (const auto& item : value["data"])
            result.data.push_back({.url = item.value("url", ""),
                .b64_json = item.value("b64_json", ""),
                .revised_prompt = item.value("revised_prompt", "")});
    return result;
}

} // namespace cnetmod::openai
