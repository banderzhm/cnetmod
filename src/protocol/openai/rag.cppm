/// cnetmod.protocol.openai:rag — Advanced retrieval augmentation pipeline

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:rag;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import :foundation;
import :messages;
import :model;
import :retrieval;
import :filters;
import nlohmann.json;

namespace cnetmod::openai {

export struct retrieval_query
{
    std::string text;
    json metadata = json::object();
    std::size_t limit = 4;
    float minimum_score = -1.0F;
    metadata_filter filter;
};

export class query_transformer
{
public:
    virtual ~query_transformer() = default;
    virtual auto transform(retrieval_query query, const run_config& config)
        -> task<std::expected<std::vector<retrieval_query>, std::string>> = 0;
};

export using query_transform_handler = std::function<task<
    std::expected<std::vector<retrieval_query>, std::string>>(
    retrieval_query query, const run_config& config)>;

export class functional_query_transformer final : public query_transformer
{
public:
    explicit functional_query_transformer(query_transform_handler handler);
    auto transform(retrieval_query query, const run_config& config)
        -> task<std::expected<std::vector<retrieval_query>, std::string>> override;

private:
    query_transform_handler handler_;
};

export enum class query_transformation
{
    compress,
    rewrite,
    expand,
    hypothetical_document
};

export struct model_query_transformer_options
{
    query_transformation strategy = query_transformation::rewrite;
    std::string model;
    std::size_t max_queries = 3;
    bool include_original = false;
};

/// Uses strict structured model output for standalone compression, rewriting,
/// multi-query expansion or hypothetical-document (HyDE) retrieval queries.
export class model_query_transformer final : public query_transformer
{
public:
    model_query_transformer(chat_model& model,
        model_query_transformer_options options = {});
    auto transform(retrieval_query query, const run_config& config)
        -> task<std::expected<std::vector<retrieval_query>, std::string>> override;

private:
    chat_model& model_;
    model_query_transformer_options options_;
};

export class query_router
{
public:
    virtual ~query_router() = default;
    virtual auto route(const retrieval_query& query, const run_config& config)
        -> task<std::expected<std::vector<retriever*>, std::string>> = 0;
};

export using query_route_handler = std::function<task<
    std::expected<std::vector<retriever*>, std::string>>(
    const retrieval_query& query, const run_config& config)>;

export class functional_query_router final : public query_router
{
public:
    explicit functional_query_router(query_route_handler handler);
    auto route(const retrieval_query& query, const run_config& config)
        -> task<std::expected<std::vector<retriever*>, std::string>> override;

private:
    query_route_handler handler_;
};

export class content_aggregator
{
public:
    virtual ~content_aggregator() = default;
    virtual auto aggregate(std::vector<std::vector<document_match>> ranked_lists,
        std::size_t limit) -> std::vector<document_match> = 0;
};

export using content_aggregate_handler = std::function<
    std::vector<document_match>(
        std::vector<std::vector<document_match>> ranked_lists,
        std::size_t limit)>;

export class functional_content_aggregator final : public content_aggregator
{
public:
    explicit functional_content_aggregator(content_aggregate_handler handler);
    auto aggregate(std::vector<std::vector<document_match>> ranked_lists,
        std::size_t limit) -> std::vector<document_match> override;

private:
    content_aggregate_handler handler_;
};

export class content_reranker
{
public:
    virtual ~content_reranker() = default;
    virtual auto rerank(std::string query,
        std::vector<document_match> documents, const run_config& config)
        -> task<std::expected<std::vector<document_match>, std::string>> = 0;
};

export using content_rerank_handler = std::function<task<
    std::expected<std::vector<document_match>, std::string>>(
    std::string query, std::vector<document_match> documents,
    const run_config& config)>;

export class functional_content_reranker final : public content_reranker
{
public:
    explicit functional_content_reranker(content_rerank_handler handler);
    auto rerank(std::string query, std::vector<document_match> documents,
        const run_config& config)
        -> task<std::expected<std::vector<document_match>, std::string>> override;

private:
    content_rerank_handler handler_;
};

export class scoring_model
{
public:
    virtual ~scoring_model() = default;
    virtual auto score(std::string query,
        const std::vector<document>& documents, const run_config& config)
        -> task<std::expected<std::vector<float>, std::string>> = 0;
};

export using scoring_handler = std::function<task<
    std::expected<std::vector<float>, std::string>>(
    std::string query, const std::vector<document>& documents,
    const run_config& config)>;

export class functional_scoring_model final : public scoring_model
{
public:
    explicit functional_scoring_model(scoring_handler handler);
    auto score(std::string query, const std::vector<document>& documents,
        const run_config& config)
        -> task<std::expected<std::vector<float>, std::string>> override;

private:
    scoring_handler handler_;
};

export struct chat_scoring_options
{
    std::string model;
    std::size_t max_document_characters = 8 * 1024;
};

/// Adapter using strict structured chat output as a relevance scoring model.
export class chat_scoring_model final : public scoring_model
{
public:
    explicit chat_scoring_model(chat_model& model,
        chat_scoring_options options = {});
    auto score(std::string query, const std::vector<document>& documents,
        const run_config& config)
        -> task<std::expected<std::vector<float>, std::string>> override;

private:
    chat_model& model_;
    chat_scoring_options options_;
};

/// Reranker assigning scores with a provider-neutral scoring strategy.
export class scoring_reranker final : public content_reranker
{
public:
    explicit scoring_reranker(scoring_model& model,
        float minimum_score = -1.0F) noexcept;
    auto rerank(std::string query, std::vector<document_match> documents,
        const run_config& config)
        -> task<std::expected<std::vector<document_match>, std::string>> override;

private:
    scoring_model& model_;
    float minimum_score_;
};

export class context_injector
{
public:
    virtual ~context_injector() = default;
    virtual auto inject(message input,
        const std::vector<document_match>& documents)
        -> std::vector<message> = 0;
};

export using context_injection_handler = std::function<std::vector<message>(
    message input, const std::vector<document_match>& documents)>;

export class functional_context_injector final : public context_injector
{
public:
    explicit functional_context_injector(context_injection_handler handler);
    auto inject(message input,
        const std::vector<document_match>& documents)
        -> std::vector<message> override;

private:
    context_injection_handler handler_;
};

export class identity_query_transformer final : public query_transformer
{
public:
    auto transform(retrieval_query query, const run_config& config)
        -> task<std::expected<std::vector<retrieval_query>, std::string>> override;
};

export class static_query_router final : public query_router
{
public:
    explicit static_query_router(std::vector<retriever*> retrievers);
    auto route(const retrieval_query& query, const run_config& config)
        -> task<std::expected<std::vector<retriever*>, std::string>> override;

private:
    std::vector<retriever*> retrievers_;
};

export struct named_retriever
{
    std::string name;
    std::string description;
    retriever* source = nullptr;
};

export enum class retrieval_route_fallback
{
    fail,
    none,
    all
};

export struct model_query_router_options
{
    std::string model;
    retrieval_route_fallback fallback = retrieval_route_fallback::all;
};

/// Uses a chat model and strict structured output to select retrievers.
export class model_query_router final : public query_router
{
public:
    model_query_router(chat_model& model,
        std::vector<named_retriever> retrievers,
        model_query_router_options options = {});

    auto route(const retrieval_query& query, const run_config& config)
        -> task<std::expected<std::vector<retriever*>, std::string>> override;

private:
    auto fallback(std::string error)
        -> std::expected<std::vector<retriever*>, std::string>;

    chat_model& model_;
    std::vector<named_retriever> retrievers_;
    model_query_router_options options_;
};

/// Two-stage reciprocal-rank fusion with deterministic de-duplication.
export class reciprocal_rank_fusion final : public content_aggregator
{
public:
    explicit reciprocal_rank_fusion(float rank_constant = 60.0F);
    auto aggregate(std::vector<std::vector<document_match>> ranked_lists,
        std::size_t limit) -> std::vector<document_match> override;

private:
    float rank_constant_;
};

export class passthrough_reranker final : public content_reranker
{
public:
    auto rerank(std::string query, std::vector<document_match> documents,
        const run_config& config)
        -> task<std::expected<std::vector<document_match>, std::string>> override;
};

export class developer_context_injector final : public context_injector
{
public:
    explicit developer_context_injector(std::string instruction =
                                            "Use the following retrieved context when relevant:\n{context}",
        std::string separator = "\n\n");
    auto inject(message input, const std::vector<document_match>& documents)
        -> std::vector<message> override;

private:
    std::string instruction_;
    std::string separator_;
};

export struct citation_context_options
{
    std::string instruction =
        "Answer using the retrieved sources when relevant. Cite sources with " "their bracketed labels:\n{context}";
    std::string separator = "\n\n";
    std::vector<std::string> metadata_fields;
    bool include_document_id = true;
    bool include_score = true;
};

/// Injects stable source labels and selected provenance metadata so callers can
/// map model citations back to the retrieved documents.
export class citation_context_injector final : public context_injector
{
public:
    explicit citation_context_injector(citation_context_options options = {});
    auto inject(message input, const std::vector<document_match>& documents)
        -> std::vector<message> override;

private:
    citation_context_options options_;
};

export struct retrieval_augmentation
{
    std::vector<message> messages;
    std::vector<document_match> documents;
    std::vector<retrieval_query> queries;
};

/// Pipeline/Strategy composition for advanced multi-query, multi-source RAG.
export class retrieval_augmentor
{
public:
    retrieval_augmentor(query_transformer& transformer, query_router& router,
        content_aggregator& aggregator, context_injector& injector,
        content_reranker* reranker = nullptr);
    retrieval_augmentor(io_context& context, query_transformer& transformer,
        query_router& router, content_aggregator& aggregator,
        context_injector& injector, content_reranker* reranker = nullptr);

    auto augment(message input, retrieval_query query,
        const run_config& config = {})
        -> task<std::expected<retrieval_augmentation, std::string>>;

private:
    query_transformer& transformer_;
    query_router& router_;
    content_aggregator& aggregator_;
    context_injector& injector_;
    content_reranker* reranker_;
    io_context* parallel_context_ = nullptr;
};

} // namespace cnetmod::openai
