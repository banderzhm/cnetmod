/// cnetmod.protocol.openai:moderation — Content moderation contracts

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:moderation;

import std;
import :foundation;

namespace cnetmod::openai {

export struct moderation_request
{
    std::string model = "text-moderation-latest";
    std::vector<std::string> input;

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct moderation_categories
{
    bool hate = false;
    bool hate_threatening = false;
    bool harassment = false;
    bool harassment_threatening = false;
    bool self_harm = false;
    bool self_harm_intent = false;
    bool self_harm_instructions = false;
    bool sexual = false;
    bool sexual_minors = false;
    bool violence = false;
    bool violence_graphic = false;
};

export struct moderation_result
{
    bool flagged = false;
    moderation_categories categories;
};

export struct moderation_response
{
    std::string id;
    std::string model;
    std::vector<moderation_result> results;

    [[nodiscard]] static auto from_json(std::string_view text)
        -> moderation_response;
};

} // namespace cnetmod::openai
