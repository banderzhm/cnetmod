/// cnetmod.protocol.openai:foundation — implementations

module;

#include <cnetmod/config.hpp>
#include "../../utils/detail/simdjson_depth.hpp"

module cnetmod.protocol.openai;

import std;
import :foundation;

namespace cnetmod::openai {

auto error_response::from_json(std::string_view text) -> error_response
{
    using cnetmod::detail::find_last_json_field;
    simdjson::dom::parser parser;
    simdjson::dom::element value;
    if (!cnetmod::detail::parse_bounded_json(parser, text, value))
        return {.message = std::string{text}};

    error_response result;
    simdjson::dom::object error;
    if (find_last_json_field(value, "error").get_object().get(error))
    {
        std::string_view message = text;
        (void)find_last_json_field(value, "message").get_string().get(message);
        result.message = message;
        return result;
    }
    for (const auto field : error)
    {
        std::string_view content;
        (void)field.value.get_string().get(content);
        if (field.key == "message")
            result.message = content;
        else if (field.key == "type")
            result.type = content;
        else if (field.key == "code")
            result.code = content;
    }
    return result;
}

} // namespace cnetmod::openai
