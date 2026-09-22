/// cnetmod.protocol.openai:moderation — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :moderation;
import cnetmod.json;

namespace cnetmod::openai {

auto moderation_request::to_json() const -> std::string
{
    json value{{"model", model}};
    if (input.size() == 1)
        value["input"] = input.front();
    else
    {
        auto inputs = cnetmod::json::array();
        for (const auto& item : input)
            inputs.get_array().emplace_back(item);
        value["input"] = std::move(inputs);
    }
    return cnetmod::json::write_document(value).value_or("{}");
}

auto moderation_response::from_json(std::string_view text)
    -> moderation_response
{
    moderation_response result;
    auto parsed_value = cnetmod::json::parse_document(text);
    if (!parsed_value)
        return result;
    const auto& value = *parsed_value;
    result.id = cnetmod::json::value_or(value, "id", std::string{});
    result.model = cnetmod::json::value_or(value, "model", std::string{});
    if (!value.contains("results") || !value["results"].is_array())
        return result;
    for (const auto& item : value["results"].get_array())
    {
        moderation_result parsed{.flagged = cnetmod::json::value_or(
                                     item, "flagged", false)};
        if (item.contains("categories") && item["categories"].is_object())
        {
            const auto& categories = item["categories"];
            parsed.categories.hate = cnetmod::json::value_or(categories, "hate", false);
            parsed.categories.hate_threatening = cnetmod::json::value_or(categories, "hate/threatening", false);
            parsed.categories.harassment = cnetmod::json::value_or(categories, "harassment", false);
            parsed.categories.harassment_threatening = cnetmod::json::value_or(categories, "harassment/threatening", false);
            parsed.categories.self_harm = cnetmod::json::value_or(categories, "self-harm", false);
            parsed.categories.self_harm_intent = cnetmod::json::value_or(categories, "self-harm/intent", false);
            parsed.categories.self_harm_instructions = cnetmod::json::value_or(categories, "self-harm/instructions", false);
            parsed.categories.sexual = cnetmod::json::value_or(categories, "sexual", false);
            parsed.categories.sexual_minors = cnetmod::json::value_or(categories, "sexual/minors", false);
            parsed.categories.violence = cnetmod::json::value_or(categories, "violence", false);
            parsed.categories.violence_graphic = cnetmod::json::value_or(categories, "violence/graphic", false);
        }
        result.results.push_back(std::move(parsed));
    }
    return result;
}

} // namespace cnetmod::openai
