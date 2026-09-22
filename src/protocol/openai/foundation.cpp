/// cnetmod.protocol.openai:foundation — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import cnetmod.json;

namespace cnetmod::openai {

auto error_response::from_json(std::string_view text) -> error_response
{
    error_response result;
    auto parsed = cnetmod::json::parse_document(text);
    if (!parsed)
    {
        result.message = std::string(text);
        return result;
    }
    const auto& value = *parsed;
    if (value.contains("error") && value["error"].is_object())
    {
        const auto& error = value["error"];
        result.message = cnetmod::json::value_or(
            error, "message", std::string{});
        result.type = cnetmod::json::value_or(error, "type", std::string{});
        result.code = cnetmod::json::value_or(error, "code", std::string{});
    }
    else
    {
        result.message = cnetmod::json::value_or(
            value, "message", std::string{text});
    }
    return result;
}

} // namespace cnetmod::openai
