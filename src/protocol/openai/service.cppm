/// cnetmod.protocol.openai:service — High-level AI application facade

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:service;

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
import cnetmod.json;

namespace cnetmod::openai {

export struct ai_service_options
{
    std::string system_instruction;
    std::string retrieved_context_instruction =
        "Use the following retrieved context when it is relevant:\n{context}";
    std::string output_retry_instruction =
        "The previous output violated the required output policy. Correct it: {reason}";
    std::size_t retrieval_limit = 4;
    float retrieval_minimum_score = -1.0F;
    std::size_t max_output_retries = 2;
    agent_options agent;
};

export struct ai_service_result
{
    message output;
    usage token_usage;
    std::vector<agent_step> tool_steps;
    std::vector<document_match> retrieved_documents;
    std::size_t output_attempts = 0;
};

/// Facade coordinating model, memory, retrieval, tools and policy middleware.
export class ai_service
{
public:
    explicit ai_service(chat_model& model, ai_service_options options = {});

    auto with_tools(tool_registry& tools) noexcept -> ai_service&;
    auto with_tool_provider(tool_provider& provider) noexcept -> ai_service&;
    auto with_memory(chat_memory& memory) noexcept -> ai_service&;
    auto with_memory_provider(chat_memory_provider provider) -> ai_service&;
    auto with_retriever(retriever& source) noexcept -> ai_service&;
    auto with_retrieval_augmentor(retrieval_augmentor& augmentor) noexcept
        -> ai_service&;
    auto with_guardrails(guardrail_pipeline& guardrails) noexcept -> ai_service&;

    auto invoke(std::string input, std::string session_id = {},
        chat_request defaults = {}, const run_config& config = {})
        -> task<std::expected<ai_service_result, std::string>>;
    auto stream(std::string input, chat_model::stream_handler handler,
        std::string session_id = {}, chat_request defaults = {},
        const run_config& config = {})
        -> task<std::expected<ai_service_result, std::string>>;

private:
    auto execute(std::string input, std::string session_id,
        chat_model::stream_handler stream_handler, chat_request defaults,
        const run_config& config)
        -> task<std::expected<ai_service_result, std::string>>;
    [[nodiscard]] auto resolve_memory(std::string_view session_id) const
        -> chat_memory*;
    [[nodiscard]] auto format_context(
        const std::vector<document_match>& documents) const -> std::string;

    chat_model& model_;
    ai_service_options options_;
    tool_registry* tools_ = nullptr;
    tool_provider* tool_provider_ = nullptr;
    chat_memory* memory_ = nullptr;
    chat_memory_provider memory_provider_;
    retriever* retriever_ = nullptr;
    retrieval_augmentor* augmentor_ = nullptr;
    guardrail_pipeline* guardrails_ = nullptr;
};

} // namespace cnetmod::openai
