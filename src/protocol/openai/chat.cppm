/// cnetmod.protocol.openai:chat — Chat Completions wire contracts

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:chat;

import std;
import :foundation;
import :tool_contracts;
import :messages;

namespace cnetmod::openai {

export struct chat_request
{
    std::string model = "gpt-4o-mini";
    std::vector<message> messages;
    double temperature = 0.7;
    int max_tokens = 4096;
    std::optional<int> max_completion_tokens;
    bool stream = false;
    double top_p = 1.0;
    double frequency_penalty = 0.0;
    double presence_penalty = 0.0;
    std::string stop;
    int n = 1;
    std::optional<int> seed;
    std::string user;
    std::string response_format;
    std::string response_schema_name = "response";
    json response_schema = json::object();
    bool response_schema_strict = true;
    std::vector<tool> tools;
    std::string tool_choice;
    json tool_choice_object;
    json extra_body = json::object();

    /**
     * @brief Adds a provider-specific textual request property.
     *
     * This keeps application adapters independent of the JSON implementation
     * used by the OpenAI-compatible wire layer.
     */
    auto set_extra_text(std::string key, std::string value) -> void;

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct chat_chunk
{
    struct tool_delta
    {
        std::size_t index = 0;
        tool_call value;
    };

    std::string id;
    std::string model;
    std::string delta_role;
    std::string delta_content;
    std::string finish_reason;
    std::vector<tool_delta> delta_tool_calls;
    std::optional<usage> token_usage;
    std::size_t generation_attempt = 1;

    [[nodiscard]] static auto from_json(std::string_view text) -> chat_chunk;
};

export struct choice
{
    int index = 0;
    message msg;
    std::string finish_reason;
};

export struct chat_response
{
    std::string id;
    std::string model;
    std::vector<choice> choices;
    usage token_usage;

    [[nodiscard]] auto content() const -> std::string_view;
    [[nodiscard]] static auto from_json(std::string_view text) -> chat_response;
};

export using on_chunk_fn = std::function<void(const chat_chunk& chunk)>;

} // namespace cnetmod::openai
