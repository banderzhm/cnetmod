/// cnetmod.protocol.openai:agent — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.task_group;
import cnetmod.io.io_context;
import :foundation;
import :tool_contracts;
import :messages;
import :chat;
import :model;
import :memory;
import :tools;
import :tool_search;
import :agent;
import cnetmod.json;

namespace cnetmod::openai {

agent_executor::agent_executor(chat_model& model, tool_registry& tools,
    chat_memory* memory, agent_options options)
    : model_(model), tools_(&tools), memory_(memory), options_(std::move(options))
{
}

auto agent_executor::with_tool_search(tool_search_strategy& strategy,
    std::string command_name) -> agent_executor&
{
    if (command_name.empty())
        throw std::invalid_argument("tool search command name cannot be empty");
    tool_search_ = &strategy;
    tool_search_command_ = std::move(command_name);
    return *this;
}

auto agent_executor::with_parallel_tool_execution(io_context& context) noexcept
    -> agent_executor&
{
    parallel_context_ = &context;
    return *this;
}

agent_executor::agent_executor(chat_model& model, tool_provider& provider,
    chat_memory* memory, agent_options options)
    : model_(model), provider_(&provider), memory_(memory), options_(std::move(options))
{
}

agent_executor::agent_executor(chat_model& model, tool_registry& tools,
    tool_provider& provider, chat_memory* memory, agent_options options)
    : model_(model), tools_(&tools), provider_(&provider), memory_(memory), options_(std::move(options))
{
}

auto agent_executor::invoke(std::string input, chat_request defaults,
    const run_config& config)
    -> task<std::expected<agent_result, std::string>>
{
    co_return co_await invoke(std::vector<message>{message::user(input)},
        std::move(defaults), config);
}

auto agent_executor::invoke(std::vector<message> input, chat_request defaults,
    const run_config& config)
    -> task<std::expected<agent_result, std::string>>
{
    co_return co_await execute(std::move(input), {}, std::move(defaults), config);
}

auto agent_executor::stream(std::string input,
    chat_model::stream_handler handler, chat_request defaults,
    const run_config& config)
    -> task<std::expected<agent_result, std::string>>
{
    co_return co_await stream(std::vector<message>{message::user(input)},
        std::move(handler), std::move(defaults), config);
}

auto agent_executor::stream(std::vector<message> input,
    chat_model::stream_handler handler, chat_request defaults,
    const run_config& config)
    -> task<std::expected<agent_result, std::string>>
{
    if (!handler)
        co_return std::unexpected("agent stream handler is not configured");
    co_return co_await execute(std::move(input), std::move(handler),
        std::move(defaults), config);
}

auto agent_executor::execute(std::vector<message> input,
    chat_model::stream_handler stream_handler, chat_request defaults,
    const run_config& config)
    -> task<std::expected<agent_result, std::string>>
{
    run_scope agent_run{config, run_event_type::agent_start,
        run_event_type::agent_end, run_event_type::agent_error, "agent"};
    auto operation_config = agent_run.child_config();
    std::vector<message> context;
    if (memory_)
    {
        auto snapshot = co_await memory_->snapshot();
        if (!snapshot)
            co_return std::unexpected("memory load failed: " + snapshot.error());
        context = std::move(*snapshot);
    }
    if (!options_.system_prompt.empty() &&
        std::ranges::none_of(context, [](const message& item)
            {
                return item.role == "system" || item.role == "developer";
            }))
        context.insert(context.begin(), message::system(options_.system_prompt));
    context.insert(context.end(), input.begin(), input.end());

    std::vector<message> new_messages = input;
    agent_result result;
    std::optional<tool_provider_result> fixed_tools;
    std::set<std::string, std::less<>> discovered_tools;

    const auto max_iterations = std::max<std::size_t>(1, options_.max_iterations);
    for (std::size_t iteration = 1; iteration <= max_iterations; ++iteration)
    {
        if (operation_config.is_cancelled())
            co_return std::unexpected("agent invocation cancelled");

        auto active_tools = tools_ ? *tools_ : tool_registry{};
        if (provider_)
        {
            if (!fixed_tools || provider_->is_dynamic())
            {
                auto provided = co_await provider_->provide({.conversation = context,
                    .session_id = operation_config.metadata.contains("session_id")
                        ? operation_config.metadata.at("session_id")
                        : std::string{},
                    .invocation_parameters = operation_config.metadata,
                    .iteration = iteration,
                    .config = &operation_config});
                if (!provided)
                    co_return std::unexpected(
                        "tool provider failed: " + provided.error());
                fixed_tools = std::move(*provided);
            }
            for (const auto& candidate : fixed_tools->tools)
            {
                auto added = active_tools.add(candidate);
                if (!added)
                    co_return std::unexpected("tool provider failed: " + added.error());
            }
        }
        if (tool_search_)
        {
            auto candidates = active_tools.commands();
            std::vector<tool> searchable;
            tool_registry visible;
            for (const auto& candidate : candidates)
            {
                if (candidate.visibility == tool_visibility::always_visible ||
                    discovered_tools.contains(
                        candidate.definition.function_name))
                {
                    auto added = visible.add(candidate);
                    if (!added)
                        co_return std::unexpected(added.error());
                }
                else
                {
                    searchable.push_back(candidate.definition);
                }
            }
            if (!searchable.empty())
            {
                auto search_command = executable_tool{
                    .definition = {
                        .function_name = tool_search_command_,
                        .function_description =
                            "Find tools relevant to a task before invoking them",
                        .function_parameters = {
                            {"type", "object"},
                            {"properties",
                                {{"query", {{"type", "string"}}},
                                    {"max_results",
                                        {{"type", "integer"},
                                            {"minimum", 1}}}}},
                            {"required", {"query"}},
                            {"additionalProperties", false}}},
                    .handler = [strategy = tool_search_, searchable, &discovered_tools, operation_config](const json& arguments) -> task<std::expected<json, std::string>>
                    {
                        const auto limit = arguments.value(
                            "max_results", std::size_t{5});
                        auto matches = co_await strategy->search(
                            {.query = arguments.value("query", ""),
                                .candidates = searchable,
                                .max_results = limit},
                            operation_config);
                        if (!matches)
                            co_return std::unexpected(matches.error());
                        auto found = json::array();
                        for (const auto& match : *matches)
                        {
                            const auto known = std::ranges::any_of(searchable,
                                [&](const auto& candidate)
                                {
                                    return candidate.function_name == match.name;
                                });
                            if (!known)
                                continue;
                            discovered_tools.insert(match.name);
                            found.push_back({{"name", match.name},
                                {"score", match.score}});
                        }
                        co_return json{{"tools", std::move(found)}};
                    },
                    .visibility = tool_visibility::always_visible};
                auto added = visible.add(std::move(search_command));
                if (!added)
                    co_return std::unexpected(
                        "tool search command conflicts with a registered tool: " +
                        added.error());
            }
            active_tools = std::move(visible);
        }
        defaults.tools = active_tools.definitions();
        if (!defaults.tools.empty() && defaults.tool_choice.empty())
            defaults.tool_choice = "auto";

        auto request = defaults;
        request.messages = context;
        std::expected<chat_response, std::string> response;
        if (stream_handler)
            response = co_await model_.stream(
                std::move(request), stream_handler, operation_config);
        else
            response = co_await model_.invoke(std::move(request), operation_config);
        if (!response)
            co_return std::unexpected(response.error());
        if (response->choices.empty())
            co_return std::unexpected("model returned no choices");

        result.iterations = iteration;
        result.token_usage.prompt_tokens += response->token_usage.prompt_tokens;
        result.token_usage.completion_tokens += response->token_usage.completion_tokens;
        result.token_usage.total_tokens += response->token_usage.total_tokens;
        auto assistant = std::move(response->choices.front().msg);
        context.push_back(assistant);
        new_messages.push_back(assistant);

        if (assistant.tool_calls.empty())
        {
            result.output = std::move(assistant);
            result.transcript = context;
            if (!options_.return_intermediate_steps)
                result.intermediate_steps.clear();
            if (memory_)
            {
                auto saved = co_await memory_->append(std::move(new_messages));
                if (!saved)
                    co_return std::unexpected("memory save failed: " + saved.error());
            }
            agent_run.succeed(result.output.content, iteration);
            co_return result;
        }

        using invocation_result = std::expected<std::string, tool_error>;
        std::vector<std::optional<invocation_result>> observations(
            assistant.tool_calls.size());
        if (parallel_context_ && assistant.tool_calls.size() > 1)
        {
            task_group group{*parallel_context_};
            for (std::size_t call_index = 0;
                call_index < assistant.tool_calls.size(); ++call_index)
            {
                const auto started = group.run(
                    [&, call_index](cancel_token&)
                        -> task<std::expected<void, std::error_code>>
                    {
                        observations[call_index].emplace(
                            co_await active_tools.invoke_detailed(
                                assistant.tool_calls[call_index], operation_config));
                        co_return std::expected<void, std::error_code>{};
                    });
                if (!started)
                    co_return std::unexpected(
                        "parallel tool execution rejected a child task");
            }
            auto joined = co_await group.join();
            if (!joined)
                co_return std::unexpected(
                    "parallel tool execution failed: " +
                    joined.error().message());
        }
        else
        {
            for (std::size_t call_index = 0;
                call_index < assistant.tool_calls.size(); ++call_index)
                observations[call_index].emplace(
                    co_await active_tools.invoke_detailed(
                        assistant.tool_calls[call_index], operation_config));
        }

        for (std::size_t call_index = 0;
            call_index < assistant.tool_calls.size(); ++call_index)
        {
            const auto& call = assistant.tool_calls[call_index];
            if (operation_config.is_cancelled())
                co_return std::unexpected("agent invocation cancelled");
            if (!observations[call_index])
                co_return std::unexpected(
                    "tool invocation completed without a result");
            auto observation = std::move(*observations[call_index]);
            std::string tool_content;
            if (observation)
            {
                tool_content = *observation;
            }
            else if (options_.handle_tool_error)
            {
                auto resolution = co_await options_.handle_tool_error(
                    call, observation.error(), operation_config);
                if (resolution.action == tool_error_action::fail_invocation)
                    co_return std::unexpected(resolution.message.empty()
                            ? observation.error().message
                            : resolution.message);
                tool_content = resolution.message.empty()
                    ? json{{"error", observation.error().message}}.dump()
                    : std::move(resolution.message);
            }
            else
            {
                if (!options_.continue_on_tool_error)
                    co_return std::unexpected(observation.error().message);
                tool_content = json{{"error", observation.error().message}}.dump();
            }
            agent_step step{.call = call,
                .observation = tool_content,
                .successful = observation.has_value()};
            auto tool_message = message::tool_result(call.id, tool_content,
                call.function.name);
            context.push_back(tool_message);
            new_messages.push_back(std::move(tool_message));
            result.intermediate_steps.push_back(std::move(step));

            const auto behavior = active_tools.behavior(call.function.name)
                                      .value_or(tool_return_behavior::to_model);
            const bool return_now = behavior == tool_return_behavior::immediate ||
                (behavior == tool_return_behavior::immediate_if_last &&
                    call_index + 1 == assistant.tool_calls.size());
            if (return_now && observation)
            {
                result.output = message::model_output(tool_content);
                context.push_back(result.output);
                result.transcript = context;
                new_messages.push_back(result.output);
                if (!options_.return_intermediate_steps)
                    result.intermediate_steps.clear();
                if (memory_)
                {
                    auto saved = co_await memory_->append(std::move(new_messages));
                    if (!saved)
                        co_return std::unexpected(
                            "memory save failed: " + saved.error());
                }
                agent_run.succeed(result.output.content, iteration);
                co_return result;
            }
        }
    }
    auto error = std::format(
        "agent stopped after reaching max_iterations={}", max_iterations);
    agent_run.fail(error, max_iterations);
    co_return std::unexpected(std::move(error));
}

} // namespace cnetmod::openai
