/// cnetmod.protocol.openai:agent — Tool-calling agent state machine

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:agent;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import :foundation;
import :tool_contracts;
import :messages;
import :chat;
import :model;
import :memory;
import :tools;
import :tool_search;

namespace cnetmod::openai {

export enum class tool_error_action
{
    return_to_model,
    fail_invocation
};

export struct tool_error_resolution
{
    tool_error_action action = tool_error_action::return_to_model;
    std::string message;
};

export using tool_error_handler = std::function<task<tool_error_resolution>(
    const tool_call& call, const tool_error& error,
    const run_config& config)>;

export struct agent_options
{
    std::size_t max_iterations = 8;
    bool continue_on_tool_error = true;
    bool return_intermediate_steps = true;
    std::string system_prompt;
    tool_error_handler handle_tool_error;
};

export struct agent_step
{
    tool_call call;
    std::string observation;
    bool successful = false;
};

export struct agent_result
{
    message output;
    std::vector<agent_step> intermediate_steps;
    std::vector<message> transcript;
    usage token_usage;
    std::size_t iterations = 0;
};

/// State pattern: model -> zero or more tool commands -> final answer.
export class agent_executor
{
public:
    agent_executor(chat_model& model, tool_registry& tools,
        chat_memory* memory = nullptr, agent_options options = {});
    agent_executor(chat_model& model, tool_provider& provider,
        chat_memory* memory = nullptr, agent_options options = {});
    agent_executor(chat_model& model, tool_registry& tools,
        tool_provider& provider, chat_memory* memory = nullptr,
        agent_options options = {});

    auto invoke(std::string input, chat_request defaults = {},
        const run_config& config = {})
        -> task<std::expected<agent_result, std::string>>;
    auto invoke(std::vector<message> input, chat_request defaults = {},
        const run_config& config = {})
        -> task<std::expected<agent_result, std::string>>;
    auto stream(std::string input, chat_model::stream_handler handler,
        chat_request defaults = {}, const run_config& config = {})
        -> task<std::expected<agent_result, std::string>>;
    auto stream(std::vector<message> input, chat_model::stream_handler handler,
        chat_request defaults = {}, const run_config& config = {})
        -> task<std::expected<agent_result, std::string>>;
    auto with_tool_search(tool_search_strategy& strategy,
        std::string command_name = "find_tools") -> agent_executor&;
    auto with_parallel_tool_execution(io_context& context) noexcept
        -> agent_executor&;

private:
    auto execute(std::vector<message> input,
        chat_model::stream_handler stream_handler, chat_request defaults,
        const run_config& config)
        -> task<std::expected<agent_result, std::string>>;

    chat_model& model_;
    tool_registry* tools_ = nullptr;
    tool_provider* provider_ = nullptr;
    tool_search_strategy* tool_search_ = nullptr;
    std::string tool_search_command_ = "find_tools";
    io_context* parallel_context_ = nullptr;
    chat_memory* memory_;
    agent_options options_;
};

} // namespace cnetmod::openai
