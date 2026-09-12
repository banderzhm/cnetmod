/// cnetmod.protocol.openai:service — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :foundation;
import :messages;
import :chat;
import :model;
import :memory;
import :tools;
import :agent;
import :retrieval;
import :rag;
import :guardrails;
import :service;

namespace cnetmod::openai {

namespace {
    auto replace_marker(std::string source, std::string_view marker,
        std::string_view value) -> std::string
    {
        if (const auto position = source.find(marker); position != std::string::npos)
            source.replace(position, marker.size(), value);
        return source;
    }
} // namespace

ai_service::ai_service(chat_model& model, ai_service_options options)
    : model_(model), options_(std::move(options))
{
}

auto ai_service::with_tools(tool_registry& tools) noexcept -> ai_service&
{
    tools_ = &tools;
    return *this;
}

auto ai_service::with_tool_provider(tool_provider& provider) noexcept
    -> ai_service&
{
    tool_provider_ = &provider;
    return *this;
}

auto ai_service::with_memory(chat_memory& memory) noexcept -> ai_service&
{
    memory_ = &memory;
    memory_provider_ = {};
    return *this;
}

auto ai_service::with_memory_provider(chat_memory_provider provider) -> ai_service&
{
    memory_provider_ = std::move(provider);
    memory_ = nullptr;
    return *this;
}

auto ai_service::with_retriever(retriever& source) noexcept -> ai_service&
{
    retriever_ = &source;
    augmentor_ = nullptr;
    return *this;
}

auto ai_service::with_retrieval_augmentor(
    retrieval_augmentor& augmentor) noexcept -> ai_service&
{
    augmentor_ = &augmentor;
    retriever_ = nullptr;
    return *this;
}

auto ai_service::with_guardrails(guardrail_pipeline& guardrails) noexcept
    -> ai_service&
{
    guardrails_ = &guardrails;
    return *this;
}

auto ai_service::resolve_memory(std::string_view session_id) const -> chat_memory*
{
    if (memory_provider_)
        return &memory_provider_(session_id);
    return memory_;
}

auto ai_service::format_context(
    const std::vector<document_match>& documents) const -> std::string
{
    std::string context;
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        if (index > 0)
            context += "\n\n";
        context += documents[index].value.page_content;
    }
    return context;
}

auto ai_service::invoke(std::string input, std::string session_id,
    chat_request defaults, const run_config& config)
    -> task<std::expected<ai_service_result, std::string>>
{
    co_return co_await execute(std::move(input), std::move(session_id), {},
        std::move(defaults), config);
}

auto ai_service::stream(std::string input, chat_model::stream_handler handler,
    std::string session_id, chat_request defaults, const run_config& config)
    -> task<std::expected<ai_service_result, std::string>>
{
    if (!handler)
        co_return std::unexpected("AI service stream handler is not configured");
    co_return co_await execute(std::move(input), std::move(session_id),
        std::move(handler), std::move(defaults), config);
}

auto ai_service::execute(std::string input, std::string session_id,
    chat_model::stream_handler stream_handler, chat_request defaults,
    const run_config& config)
    -> task<std::expected<ai_service_result, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("AI service invocation cancelled");

    auto input_message = message::user(input);
    if (guardrails_)
    {
        auto guarded = co_await guardrails_->apply_input(
            std::move(input_message), config);
        if (!guarded)
            co_return std::unexpected("input policy rejected request: " + guarded.error());
        input_message = std::move(*guarded);
    }

    auto* memory = resolve_memory(session_id);
    std::vector<message> messages;
    if (memory)
    {
        auto snapshot = co_await memory->snapshot();
        if (!snapshot)
            co_return std::unexpected("memory load failed: " + snapshot.error());
        messages = std::move(*snapshot);
    }
    if (!options_.system_instruction.empty() &&
        std::ranges::none_of(messages, [](const message& value)
            {
                return value.role == "system" || value.role == "developer";
            }))
        messages.insert(messages.begin(),
            message::developer(options_.system_instruction));

    ai_service_result result;
    bool input_injected = false;
    if (augmentor_)
    {
        auto augmentation = co_await augmentor_->augment(input_message,
            {.text = input_message.content,
                .limit = options_.retrieval_limit,
                .minimum_score = options_.retrieval_minimum_score},
            config);
        if (!augmentation)
            co_return std::unexpected("retrieval augmentation failed: " +
                augmentation.error());
        result.retrieved_documents = std::move(augmentation->documents);
        messages.insert(messages.end(),
            std::make_move_iterator(augmentation->messages.begin()),
            std::make_move_iterator(augmentation->messages.end()));
        input_injected = true;
    }
    else if (retriever_)
    {
        auto documents = co_await retriever_->retrieve(input_message.content,
            options_.retrieval_limit, options_.retrieval_minimum_score);
        if (!documents)
            co_return std::unexpected("retrieval failed: " + documents.error());
        result.retrieved_documents = std::move(*documents);
        if (!result.retrieved_documents.empty())
        {
            auto instruction = replace_marker(
                options_.retrieved_context_instruction, "{context}",
                format_context(result.retrieved_documents));
            messages.push_back(message::developer(instruction));
        }
    }
    if (!input_injected)
        messages.push_back(input_message);

    const auto attempts = options_.max_output_retries + 1;
    for (std::size_t attempt = 1; attempt <= attempts; ++attempt)
    {
        if (config.is_cancelled())
            co_return std::unexpected("AI service invocation cancelled");

        message output;
        usage token_usage;
        std::vector<agent_step> tool_steps;
        std::vector<message> conversation_delta;
        chat_model::stream_handler attempt_handler;
        if (stream_handler)
        {
            attempt_handler = [stream_handler, attempt](const chat_chunk& chunk)
                -> task<bool>
            {
                auto forwarded = chunk;
                forwarded.generation_attempt = attempt;
                co_return co_await stream_handler(forwarded);
            };
        }
        if ((tools_ && tools_->size() > 0) || tool_provider_)
        {
            auto empty_tools = tool_registry{};
            auto& registered_tools = tools_ ? *tools_ : empty_tools;
            std::optional<agent_executor> executor;
            if (tool_provider_)
                executor.emplace(model_, registered_tools, *tool_provider_,
                    nullptr, options_.agent);
            else
                executor.emplace(model_, registered_tools, nullptr,
                    options_.agent);
            auto invocation_config = config;
            if (!session_id.empty())
                invocation_config.metadata.insert_or_assign(
                    "session_id", session_id);
            std::expected<agent_result, std::string> agent_result;
            if (attempt_handler)
                agent_result = co_await executor->stream(
                    messages, attempt_handler, defaults, invocation_config);
            else
                agent_result = co_await executor->invoke(
                    messages, defaults, invocation_config);
            if (!agent_result)
                co_return std::unexpected(agent_result.error());
            output = std::move(agent_result->output);
            token_usage = agent_result->token_usage;
            tool_steps = std::move(agent_result->intermediate_steps);
            if (agent_result->transcript.size() < messages.size())
                co_return std::unexpected(
                    "agent returned a truncated conversation transcript");
            conversation_delta.insert(conversation_delta.end(),
                agent_result->transcript.begin() +
                    static_cast<std::ptrdiff_t>(messages.size()),
                agent_result->transcript.end());
        }
        else
        {
            auto request = defaults;
            request.messages = messages;
            std::expected<chat_response, std::string> response;
            if (attempt_handler)
                response = co_await model_.stream(
                    std::move(request), attempt_handler, config);
            else
                response = co_await model_.invoke(std::move(request), config);
            if (!response)
                co_return std::unexpected(response.error());
            if (response->choices.empty())
                co_return std::unexpected("model returned no choices");
            output = std::move(response->choices.front().msg);
            token_usage = response->token_usage;
            conversation_delta.push_back(output);
        }

        result.token_usage.prompt_tokens += token_usage.prompt_tokens;
        result.token_usage.completion_tokens += token_usage.completion_tokens;
        result.token_usage.total_tokens += token_usage.total_tokens;
        result.tool_steps.insert(result.tool_steps.end(),
            std::make_move_iterator(tool_steps.begin()),
            std::make_move_iterator(tool_steps.end()));
        result.output_attempts = attempt;

        if (guardrails_)
        {
            auto guarded = co_await guardrails_->apply_output(output, config);
            if (!guarded)
                co_return std::unexpected("output policy failed: " + guarded.error());
            if (guarded->action == guardrail_action::retry && attempt < attempts)
            {
                messages.insert(messages.end(), conversation_delta.begin(),
                    conversation_delta.end());
                messages.push_back(message::developer(replace_marker(
                    options_.output_retry_instruction, "{reason}", guarded->reason)));
                continue;
            }
            if (guarded->action == guardrail_action::reject ||
                guarded->action == guardrail_action::retry)
                co_return std::unexpected("output policy rejected response: " +
                    guarded->reason);
            if (guarded->replacement)
            {
                output = std::move(*guarded->replacement);
                if (!conversation_delta.empty())
                    conversation_delta.back() = output;
            }
        }

        result.output = std::move(output);
        if (memory)
        {
            std::vector<message> committed;
            committed.reserve(conversation_delta.size() + 1);
            committed.push_back(input_message);
            committed.insert(committed.end(),
                std::make_move_iterator(conversation_delta.begin()),
                std::make_move_iterator(conversation_delta.end()));
            auto saved = co_await memory->append(std::move(committed));
            if (!saved)
                co_return std::unexpected("memory save failed: " + saved.error());
        }
        co_return result;
    }
    co_return std::unexpected("output policy retry budget exhausted");
}

} // namespace cnetmod::openai
