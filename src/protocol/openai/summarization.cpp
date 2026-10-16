/// cnetmod.protocol.openai:summarization — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :messages;
import :summarization;

namespace cnetmod::openai {

namespace {
auto append_escaped(std::string& output, std::string_view value) -> void
{
    for (const auto character : value)
    {
        switch (character)
        {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        default:
            output.push_back(character);
            break;
        }
    }
}

auto append_transcript_message(std::string& transcript, const message& value,
    std::size_t index) -> void
{
    transcript += std::format("<message index=\"{}\" role=\"", index);
    append_escaped(transcript, value.role);
    transcript += "\">\n";
    if (!value.name.empty())
    {
        transcript += "name: ";
        append_escaped(transcript, value.name);
        transcript += '\n';
    }
    if (!value.content.empty())
    {
        transcript += "content: ";
        append_escaped(transcript, value.content);
        transcript += '\n';
    }
    for (const auto& part : value.content_parts)
    {
        if (!part.text.empty())
        {
            transcript += "text: ";
            append_escaped(transcript, part.text);
            transcript += '\n';
        }
        if (!part.image_url.url.empty())
        {
            transcript += "image: ";
            append_escaped(transcript, part.image_url.url);
            transcript += '\n';
        }
    }
    for (const auto& call : value.tool_calls)
    {
        transcript += "tool_call: ";
        append_escaped(transcript, call.function.name);
        transcript += '(';
        append_escaped(transcript, call.function.arguments);
        transcript += ") id=";
        append_escaped(transcript, call.id);
        transcript += '\n';
    }
    if (!value.tool_call_id.empty())
    {
        transcript += "tool_call_id: ";
        append_escaped(transcript, value.tool_call_id);
        transcript += '\n';
    }
    transcript += "</message>\n";
}
} // namespace

model_conversation_summarizer::model_conversation_summarizer(chat_model& model,
    model_conversation_summarizer_options options)
    : model_(model), options_(std::move(options))
{
    if (options_.request.max_tokens <= 0)
        options_.request.max_tokens = 1024;
    options_.request.stream = false;
    options_.request.tools.clear();
    options_.request.tool_choice.clear();
}

auto model_conversation_summarizer::summarize(
    conversation_summary_request request)
    -> task<std::expected<std::string, std::string>>
{
    if (request.messages.empty())
        co_return std::unexpected("conversation summary input cannot be empty");

    std::string transcript;
    transcript.reserve(256 * request.messages.size());
    transcript += "<conversation_transcript>\n";
    for (std::size_t index = 0; index < request.messages.size(); ++index)
        append_transcript_message(transcript, request.messages[index], index);
    transcript += "</conversation_transcript>";

    auto wire = options_.request;
    wire.messages.clear();
    wire.messages.push_back(message::system(options_.system_prompt));
    wire.messages.push_back(message::user(transcript));
    if (request.target_tokens > 0)
    {
        const auto capped = std::min<std::size_t>(request.target_tokens,
            static_cast<std::size_t>(std::numeric_limits<int>::max()));
        wire.max_tokens = std::min(wire.max_tokens, static_cast<int>(capped));
    }

    auto response = co_await model_.invoke(std::move(wire));
    if (!response)
        co_return std::unexpected(response.error());
    auto content = std::string(response->content());
    if (content.empty())
        co_return std::unexpected("conversation summarizer returned empty content");
    co_return content;
}

} // namespace cnetmod::openai
