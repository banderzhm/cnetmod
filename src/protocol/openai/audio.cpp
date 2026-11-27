/// cnetmod.protocol.openai:audio — implementations

module;

#include <cnetmod/config.hpp>
#include "../../utils/detail/simdjson_depth.hpp"

module cnetmod.protocol.openai;

import std;
import :audio;

namespace cnetmod::openai {

auto tts_request::to_json() const -> std::string
{
    simdjson::builder::string_builder builder;
    builder.start_object();
    builder.append_key_value("input", std::string_view{input});
    builder.append_comma();
    builder.append_key_value("model", std::string_view{model});
    if (response_format != "mp3")
    {
        builder.append_comma();
        builder.append_key_value("response_format", std::string_view{response_format});
    }
    if (speed != 1.0)
    {
        builder.append_comma();
        if (std::isfinite(speed))
            builder.append_key_value("speed", speed);
        else
            builder.append_key_value("speed", nullptr);
    }
    builder.append_comma();
    builder.append_key_value("voice", std::string_view{voice});
    builder.end_object();
    std::string_view output;
    if (builder.view().get(output) || !builder.validate_unicode())
        return "{}";
    return std::string{output};
}

auto transcription_response::from_json(std::string_view text_data)
    -> transcription_response
{
    using cnetmod::detail::find_last_json_field;
    simdjson::dom::parser parser;
    simdjson::dom::element value;
    if (!cnetmod::detail::parse_bounded_json(parser, text_data, value))
        return {.text = std::string{text_data}};

    transcription_response result;
    std::string_view field;
    if (!find_last_json_field(value, "text").get_string().get(field))
        result.text = field;
    if (!find_last_json_field(value, "language").get_string().get(field))
        result.language = field;
    (void)find_last_json_field(value, "duration").get_double().get(result.duration);
    return result;
}

} // namespace cnetmod::openai
