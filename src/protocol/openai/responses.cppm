/// cnetmod.protocol.openai:responses — Responses API wire contracts

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:responses;

import std;
import :foundation;
import :tool_contracts;
import :messages;

namespace cnetmod::openai {

export struct response_request
{
    std::string model = "gpt-4.1-mini";
    std::vector<message> input;

    struct tool_output
    {
        std::string call_id;
        std::string output;
    };

    std::vector<tool_output> tool_outputs;
    std::vector<json> additional_input_items;
    std::string instructions;
    std::vector<tool> tools;
    std::vector<json> additional_tools;
    std::string tool_choice;
    json tool_choice_object;
    std::string previous_response_id;
    std::optional<int> max_output_tokens;
    std::optional<int> max_tool_calls;
    std::optional<double> temperature;
    bool parallel_tool_calls = true;
    bool store = false;
    std::string response_schema_name = "response";
    json response_schema = cnetmod::json::object();
    bool response_schema_strict = true;
    std::map<std::string, std::string> metadata;
    std::string service_tier;
    std::string prompt_cache_key;
    std::string safety_identifier;
    json reasoning = cnetmod::json::object();
    json extra_body = cnetmod::json::object();

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct response_result
{
    std::string id;
    std::string model;
    std::string status;
    std::string output_text;
    std::vector<tool_call> tool_calls;
    usage token_usage;
    json raw = cnetmod::json::object();

    [[nodiscard]] static auto from_json(std::string_view text) -> response_result;
};

} // namespace cnetmod::openai
