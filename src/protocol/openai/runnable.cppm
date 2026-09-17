/// cnetmod.protocol.openai:runnable — Composable async pipeline

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:runnable;

import std;
import cnetmod.coro.task;
import :foundation;
import :messages;
import :chat;
import :model;
import :prompt;

namespace cnetmod::openai {

export using runnable_value = std::variant<std::monostate, prompt_variables,
    prompt_context, std::string, std::vector<message>, chat_response, json>;
export using runnable_step = std::function<task<
    std::expected<runnable_value, std::string>>(runnable_value,
    const run_config&)>;

/// Composite pipeline analogous to LangChain's RunnableSequence.
export class runnable
{
public:
    runnable() = default;
    explicit runnable(runnable_step step);
    auto pipe(runnable_step step) const -> runnable;
    auto invoke(runnable_value input, const run_config& config = {}) const
        -> task<std::expected<runnable_value, std::string>>;

private:
    std::vector<runnable_step> steps_;
};

export auto prompt_runnable(chat_prompt_template prompt) -> runnable_step;
export auto model_runnable(chat_model& model, chat_request defaults = {})
    -> runnable_step;
export auto parser_runnable(std::shared_ptr<const output_parser> parser)
    -> runnable_step;

} // namespace cnetmod::openai
