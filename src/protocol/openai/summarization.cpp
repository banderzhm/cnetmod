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

    auto transcript_message(const message& value, std::size_t index) -> std::string
    {
        std::string transcript;
        transcript.reserve(value.content.size() + 128);
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
        return transcript;
    }

    [[nodiscard]] auto retryable_summary_error(std::string_view error) -> bool
    {
        std::string normalized{error};
        std::ranges::transform(normalized, normalized.begin(),
            [](unsigned char symbol)
            {
                return static_cast<char>(std::tolower(symbol));
            });
        return normalized.contains("context length") ||
            normalized.contains("context window") ||
            normalized.contains("context limit") ||
            normalized.contains("maximum context") ||
            normalized.contains("too many tokens") ||
            normalized.contains("token limit") ||
            normalized.contains("maximum tokens") ||
            normalized.contains("input length") ||
            normalized.contains("maximum length") ||
            normalized.contains("too long") || normalized.contains("too large") ||
            normalized.contains("capacity") || normalized.contains("exceed");
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

    std::vector<std::string> event_strings;
    event_strings.reserve(request.messages.size());
    for (std::size_t index = 0; index < request.messages.size(); ++index)
        event_strings.push_back(
            transcript_message(request.messages[index], index));

    auto wire = options_.request;
    wire.messages.clear();
    wire.messages.push_back(message::system(options_.system_prompt));
    if (request.target_tokens > 0)
    {
        const auto capped = std::min<std::size_t>(request.target_tokens,
            static_cast<std::size_t>(std::numeric_limits<int>::max()));
        wire.max_tokens = std::min(wire.max_tokens, static_cast<int>(capped));
    }

    std::size_t max_event_characters = 0;
    std::string last_error = "conversation summarizer failed";
    const auto attempts = std::max<std::size_t>(
        1, options_.hard_reset_max_attempts);
    for (std::size_t attempt = 0; attempt < attempts; ++attempt)
    {
        std::string transcript;
        transcript.reserve(256 * event_strings.size());
        transcript += "<conversation_transcript>\n";
        for (const auto& event_string : event_strings)
        {
            if (max_event_characters == 0 ||
                event_string.size() <= max_event_characters)
            {
                transcript += event_string;
                continue;
            }
            transcript.append(event_string, 0, max_event_characters);
            transcript += std::format(
                "\n[event truncated: {} additional characters omitted]\n",
                event_string.size() - max_event_characters);
        }
        transcript += "</conversation_transcript>";

        auto invocation = wire;
        invocation.messages.push_back(message::user(std::move(transcript)));
        auto response = co_await model_.invoke(std::move(invocation));
        if (response)
        {
            auto content = std::string(response->content());
            if (content.empty())
                co_return std::unexpected(
                    "conversation summarizer returned empty content");
            co_return content;
        }
        last_error = response.error();
        if (attempt + 1 >= attempts || !retryable_summary_error(last_error))
            break;
        if (max_event_characters == 0)
        {
            for (const auto& event_string : event_strings)
                max_event_characters =
                    std::max(max_event_characters, event_string.size());
        }
        max_event_characters = std::max<std::size_t>(256,
            static_cast<std::size_t>(
                static_cast<double>(max_event_characters) *
                options_.hard_reset_context_scaling));
    }
    co_return std::unexpected(std::move(last_error));
}

} // namespace cnetmod::openai
