/// cnetmod.protocol.openai:runnable — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :foundation;
import :messages;
import :chat;
import :model;
import :prompt;
import :runnable;

namespace cnetmod::openai {

runnable::runnable(runnable_step step)
{
    steps_.push_back(std::move(step));
}

auto runnable::pipe(runnable_step step) const -> runnable
{
    auto result = *this;
    result.steps_.push_back(std::move(step));
    return result;
}

auto runnable::invoke(runnable_value input, const run_config& config) const
    -> task<std::expected<runnable_value, std::string>>
{
    auto current = std::expected<runnable_value, std::string>{std::move(input)};
    for (const auto& step : steps_)
    {
        if (config.is_cancelled())
            co_return std::unexpected("runnable invocation cancelled");
        current = co_await step(std::move(*current), config);
        if (!current)
            co_return std::unexpected(current.error());
    }
    co_return current;
}

auto prompt_runnable(chat_prompt_template prompt) -> runnable_step
{
    return [prompt = std::move(prompt)](runnable_value input,
               const run_config&) -> task<std::expected<runnable_value, std::string>>
    {
        std::expected<std::vector<message>, std::string> messages =
            std::unexpected("prompt runnable expects prompt_variables or prompt_context");
        if (const auto* variables = std::get_if<prompt_variables>(&input))
            messages = prompt.format(*variables);
        else if (const auto* context = std::get_if<prompt_context>(&input))
            messages = prompt.format_context(*context);
        if (!messages)
            co_return std::unexpected(messages.error());
        co_return runnable_value{std::move(*messages)};
    };
}

auto model_runnable(chat_model& model, chat_request defaults) -> runnable_step
{
    return [&model, defaults = std::move(defaults)](runnable_value input,
               const run_config& config) mutable
               -> task<std::expected<runnable_value, std::string>>
    {
        auto request = defaults;
        if (auto messages = std::get_if<std::vector<message>>(&input))
            request.messages.insert(request.messages.end(), messages->begin(), messages->end());
        else if (auto text = std::get_if<std::string>(&input))
            request.messages.push_back(message::user(*text));
        else
            co_return std::unexpected("model runnable expects messages or string");
        auto response = co_await model.invoke(std::move(request), config);
        if (!response)
            co_return std::unexpected(response.error());
        co_return runnable_value{std::move(*response)};
    };
}

auto parser_runnable(std::shared_ptr<const output_parser> parser) -> runnable_step
{
    return [parser = std::move(parser)](runnable_value input,
               const run_config&) -> task<std::expected<runnable_value, std::string>>
    {
        if (!parser)
            co_return std::unexpected("parser runnable has no parser");
        std::string_view text;
        if (auto response = std::get_if<chat_response>(&input))
            text = response->content();
        else if (auto value = std::get_if<std::string>(&input))
            text = *value;
        else
            co_return std::unexpected("parser runnable expects chat_response or string");
        auto parsed = parser->parse(text);
        if (!parsed)
            co_return std::unexpected(parsed.error());
        co_return runnable_value{std::move(*parsed)};
    };
}

} // namespace cnetmod::openai
