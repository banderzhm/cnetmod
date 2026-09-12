/// cnetmod.protocol.openai:audio — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :audio;

namespace cnetmod::openai {

auto tts_request::to_json() const -> std::string
{
    json value{{"model", model}, {"input", input}, {"voice", voice}};
    if (response_format != "mp3")
        value["response_format"] = response_format;
    if (speed != 1.0)
        value["speed"] = speed;
    return value.dump();
}

auto transcription_response::from_json(std::string_view text_data)
    -> transcription_response
{
    transcription_response result;
    auto value = json::parse(text_data, nullptr, false);
    if (value.is_discarded())
    {
        result.text = std::string(text_data);
        return result;
    }
    result.text = value.value("text", "");
    result.language = value.value("language", "");
    result.duration = value.value("duration", 0.0);
    return result;
}

} // namespace cnetmod::openai
