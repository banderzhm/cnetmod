/// cnetmod.protocol.openai:moderation — implementations

module;

#include <cnetmod/config.hpp>
#include "../../utils/detail/simdjson_depth.hpp"

module cnetmod.protocol.openai;

import std;
import :moderation;

namespace cnetmod::openai {

auto moderation_request::to_json() const -> std::string
{
    simdjson::builder::string_builder builder;
    builder.start_object();
    if (input.size() == 1)
        builder.append_key_value("input", std::string_view{input.front()});
    else
        builder.append_key_value("input", std::span<const std::string>{input});
    builder.append_comma();
    builder.append_key_value("model", std::string_view{model});
    builder.end_object();
    std::string_view output;
    if (builder.view().get(output) || !builder.validate_unicode())
        return "{}";
    return std::string{output};
}

auto moderation_response::from_json(std::string_view text)
    -> moderation_response
{
    using cnetmod::detail::find_last_json_field;
    simdjson::dom::parser parser;
    simdjson::dom::element value;
    if (!cnetmod::detail::parse_bounded_json(parser, text, value))
        return {};

    moderation_response result;
    std::string_view field;
    if (!find_last_json_field(value, "id").get_string().get(field))
        result.id = field;
    if (!find_last_json_field(value, "model").get_string().get(field))
        result.model = field;

    simdjson::dom::array results;
    if (find_last_json_field(value, "results").get_array().get(results))
        return result;
    result.results.reserve(results.size());
    for (const auto item : results)
    {
        auto& parsed = result.results.emplace_back();
        (void)find_last_json_field(item, "flagged").get_bool().get(parsed.flagged);
        simdjson::dom::element categories;
        if (find_last_json_field(item, "categories").get(categories))
            continue;
        const auto read = [&](std::string_view key) {
            bool enabled = false;
            (void)find_last_json_field(categories, key).get_bool().get(enabled);
            return enabled;
        };
        parsed.categories = {
            .hate = read("hate"),
            .hate_threatening = read("hate/threatening"),
            .harassment = read("harassment"),
            .harassment_threatening = read("harassment/threatening"),
            .self_harm = read("self-harm"),
            .self_harm_intent = read("self-harm/intent"),
            .self_harm_instructions = read("self-harm/instructions"),
            .sexual = read("sexual"),
            .sexual_minors = read("sexual/minors"),
            .violence = read("violence"),
            .violence_graphic = read("violence/graphic"),
        };
    }
    return result;
}

} // namespace cnetmod::openai
