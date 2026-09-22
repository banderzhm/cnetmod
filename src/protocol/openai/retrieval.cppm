/// cnetmod.protocol.openai:retrieval — Embedding-backed retrieval

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:retrieval;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :foundation;
import :messages;
import :model;
import :prompt;
import :filters;
import cnetmod.json;

namespace cnetmod::openai {

export struct document
{
    std::string id;
    std::string page_content;
    json metadata = cnetmod::json::object();
};

export struct document_match
{
    document value;
    float score = 0.0F;
};

export struct retrieval_request
{
    std::string query;
    std::size_t limit = 4;
    float minimum_score = -1.0F;
    metadata_filter filter;
    /// Non-owning invocation context. Asynchronous handlers must not retain
    /// this pointer after search() completes.
    const run_config* config = nullptr;
};

export class retriever
{
public:
    virtual ~retriever() = default;
    virtual auto retrieve(std::string query, std::size_t limit = 4,
        float minimum_score = -1.0F)
        -> task<std::expected<std::vector<document_match>, std::string>> = 0;
    virtual auto search(retrieval_request request)
        -> task<std::expected<std::vector<document_match>, std::string>>;
};

export using retrieval_handler = std::function<task<
    std::expected<std::vector<document_match>, std::string>>(
    retrieval_request request)>;

/// Adapter for search engines, databases and application retrieval functions.
export class functional_retriever final : public retriever
{
public:
    explicit functional_retriever(retrieval_handler handler);
    auto retrieve(std::string query, std::size_t limit = 4,
        float minimum_score = -1.0F)
        -> task<std::expected<std::vector<document_match>, std::string>> override;
    auto search(retrieval_request request)
        -> task<std::expected<std::vector<document_match>, std::string>> override;

private:
    retrieval_handler handler_;
};

export class embedding_store : public retriever
{
public:
    virtual auto add_documents(std::vector<document> documents)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto remove_documents(std::vector<std::string> ids)
        -> task<std::expected<std::size_t, std::string>> = 0;
    virtual auto remove_documents(metadata_filter filter)
        -> task<std::expected<std::size_t, std::string>> = 0;
    virtual auto clear() -> task<std::expected<void, std::string>> = 0;
    virtual auto size()
        -> task<std::expected<std::size_t, std::string>> = 0;
};

export struct embedding_store_handlers
{
    std::function<task<std::expected<void, std::string>>(
        std::vector<document> documents)>
        add;
    retrieval_handler search;
    std::function<task<std::expected<std::size_t, std::string>>(
        std::vector<std::string> ids)>
        remove_ids;
    std::function<task<std::expected<std::size_t, std::string>>(
        metadata_filter filter)>
        remove_filter;
    std::function<task<std::expected<void, std::string>>()> clear;
    std::function<task<std::expected<std::size_t, std::string>>()> size;
};

/// Adapter for external vector databases while preserving the store contract.
export class delegating_embedding_store final : public embedding_store
{
public:
    explicit delegating_embedding_store(embedding_store_handlers handlers);

    auto add_documents(std::vector<document> documents)
        -> task<std::expected<void, std::string>> override;
    auto retrieve(std::string query, std::size_t limit = 4,
        float minimum_score = -1.0F)
        -> task<std::expected<std::vector<document_match>, std::string>> override;
    auto search(retrieval_request request)
        -> task<std::expected<std::vector<document_match>, std::string>> override;
    auto remove_documents(std::vector<std::string> ids)
        -> task<std::expected<std::size_t, std::string>> override;
    auto remove_documents(metadata_filter filter)
        -> task<std::expected<std::size_t, std::string>> override;
    auto clear() -> task<std::expected<void, std::string>> override;
    auto size() -> task<std::expected<std::size_t, std::string>> override;

private:
    embedding_store_handlers handlers_;
};

/// Repository backed by normalized vectors and cosine similarity. Embedding
/// requests remain asynchronous; CPU scoring is offloaded to thread_pool.
export class in_memory_vector_store final : public embedding_store
{
public:
    in_memory_vector_store(io_context& context, thread_pool& pool,
        embedding_model& embeddings);

    auto add_documents(std::vector<document> documents)
        -> task<std::expected<void, std::string>>;
    auto retrieve(std::string query, std::size_t limit = 4,
        float minimum_score = -1.0F)
        -> task<std::expected<std::vector<document_match>, std::string>> override;
    auto search(retrieval_request request)
        -> task<std::expected<std::vector<document_match>, std::string>> override;
    auto remove_documents(std::vector<std::string> ids)
        -> task<std::expected<std::size_t, std::string>> override;
    auto remove_documents(metadata_filter filter)
        -> task<std::expected<std::size_t, std::string>> override;
    auto clear() -> task<std::expected<void, std::string>> override;
    auto size() -> task<std::expected<std::size_t, std::string>> override;

private:
    struct entry
    {
        document value;
        std::vector<float> embedding;
    };

    io_context& context_;
    thread_pool& pool_;
    embedding_model& embeddings_;
    async_mutex mutex_;
    std::vector<entry> entries_;
    std::optional<std::size_t> dimensions_;
};

export struct retrieval_chain_options
{
    std::size_t limit = 4;
    float minimum_score = -1.0F;
    std::string input_variable = "input";
    std::string context_variable = "context";
    std::string document_separator = "\n\n";
};

export struct retrieval_result
{
    message output;
    std::vector<document_match> documents;
    usage token_usage;
};

/// Retrieval-augmented generation pipeline: retrieve -> format -> model.
export class retrieval_chain
{
public:
    retrieval_chain(retriever& source, chat_model& model,
        chat_prompt_template prompt, retrieval_chain_options options = {});

    auto invoke(std::string input, chat_request defaults = {},
        const run_config& config = {})
        -> task<std::expected<retrieval_result, std::string>>;

private:
    retriever& source_;
    chat_model& model_;
    chat_prompt_template prompt_;
    retrieval_chain_options options_;
};

} // namespace cnetmod::openai
