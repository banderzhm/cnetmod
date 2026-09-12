/// cnetmod.protocol.openai:foundation — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;

namespace cnetmod::openai {

auto error_response::from_json(std::string_view text) -> error_response
{
    error_response result;
    auto value = json::parse(text, nullptr, false);
    if (value.is_discarded())
    {
        result.message = std::string(text);
        return result;
    }
    if (value.contains("error") && value["error"].is_object())
    {
        const auto& error = value["error"];
        result.message = error.value("message", "");
        result.type = error.value("type", "");
        result.code = error.value("code", "");
    }
    else
    {
        result.message = value.value("message", std::string(text));
    }
    return result;
}

} // namespace cnetmod::openai
