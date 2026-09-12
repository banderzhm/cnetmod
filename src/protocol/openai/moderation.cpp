/// cnetmod.protocol.openai:moderation — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :moderation;

namespace cnetmod::openai {

auto moderation_request::to_json() const -> std::string
{
    json value{{"model", model}};
    value["input"] = input.size() == 1 ? json(input.front()) : json(input);
    return value.dump();
}

auto moderation_response::from_json(std::string_view text)
    -> moderation_response
{
    moderation_response result;
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
        return result;
    result.id = value.value("id", "");
    result.model = value.value("model", "");
    if (!value.contains("results") || !value["results"].is_array())
        return result;
    for (const auto& item : value["results"])
    {
        moderation_result parsed{.flagged = item.value("flagged", false)};
        if (item.contains("categories") && item["categories"].is_object())
        {
            const auto& categories = item["categories"];
            parsed.categories.hate = categories.value("hate", false);
            parsed.categories.hate_threatening = categories.value("hate/threatening", false);
            parsed.categories.harassment = categories.value("harassment", false);
            parsed.categories.harassment_threatening = categories.value("harassment/threatening", false);
            parsed.categories.self_harm = categories.value("self-harm", false);
            parsed.categories.self_harm_intent = categories.value("self-harm/intent", false);
            parsed.categories.self_harm_instructions = categories.value("self-harm/instructions", false);
            parsed.categories.sexual = categories.value("sexual", false);
            parsed.categories.sexual_minors = categories.value("sexual/minors", false);
            parsed.categories.violence = categories.value("violence", false);
            parsed.categories.violence_graphic = categories.value("violence/graphic", false);
        }
        result.results.push_back(std::move(parsed));
    }
    return result;
}

} // namespace cnetmod::openai
