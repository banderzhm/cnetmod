/// cnetmod.protocol.openai:audio — Speech synthesis and recognition contracts

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:audio;

import std;
import :foundation;

namespace cnetmod::openai {

export struct tts_request
{
    std::string model = "tts-1";
    std::string input;
    std::string voice = "alloy";
    std::string response_format = "mp3";
    double speed = 1.0;

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct transcription_request
{
    std::vector<std::byte> file;
    std::string filename = "audio.mp3";
    std::string model = "whisper-1";
    std::string language;
    std::string prompt;
    std::string response_format = "json";
    double temperature = 0.0;
};

export struct transcription_response
{
    std::string text;
    std::string language;
    double duration = 0.0;

    [[nodiscard]] static auto from_json(std::string_view text_data)
        -> transcription_response;
};

export struct translation_request
{
    std::vector<std::byte> file;
    std::string filename = "audio.mp3";
    std::string model = "whisper-1";
    std::string prompt;
    std::string response_format = "json";
    double temperature = 0.0;
};

} // namespace cnetmod::openai
