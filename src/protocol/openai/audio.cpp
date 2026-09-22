/// cnetmod.protocol.openai:audio — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :audio;
import cnetmod.json;

namespace cnetmod::openai {

auto tts_request::to_json() const -> std::string
{
    json value{{"model", model}, {"input", input}, {"voice", voice}};
    if (response_format != "mp3")
        value["response_format"] = response_format;
    if (speed != 1.0)
        value["speed"] = speed;
    return cnetmod::json::write_document(value).value_or("{}");
}

auto transcription_response::from_json(std::string_view text_data)
    -> transcription_response
{
    transcription_response result;
    auto parsed = cnetmod::json::parse_document(text_data);
    if (!parsed)
    {
        result.text = std::string(text_data);
        return result;
    }
    const auto& value = *parsed;
    result.text = cnetmod::json::value_or(value, "text", std::string{});
    result.language = cnetmod::json::value_or(
        value, "language", std::string{});
    result.duration = cnetmod::json::value_or(value, "duration", 0.0);
    return result;
}

} // namespace cnetmod::openai
