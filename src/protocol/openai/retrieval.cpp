/// cnetmod.protocol.openai:retrieval — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :foundation;
import :messages;
import :model;
import :prompt;
import :filters;
import :retrieval;

namespace cnetmod::openai {

namespace {
    auto normalize(std::vector<float> value) -> std::vector<float>
    {
        const auto squared = std::transform_reduce(value.begin(), value.end(),
            value.begin(), 0.0F);
        const auto norm = std::sqrt(squared);
        if (norm > 0.0F)
            for (auto& component : value)
                component /= norm;
        return value;
    }

    auto similarity(const std::vector<float>& left, const std::vector<float>& right)
        -> float
    {
        if (left.size() != right.size() || left.empty())
            return -1.0F;
        return std::inner_product(left.begin(), left.end(), right.begin(), 0.0F);
    }
} // namespace

auto retriever::search(retrieval_request request)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    if (request.config && request.config->is_cancelled())
        co_return std::unexpected("retrieval cancelled");
    auto matches = co_await retrieve(std::move(request.query), request.limit,
        request.minimum_score);
    if (!matches)
        co_return std::unexpected(matches.error());
    if (!request.filter.empty())
        std::erase_if(*matches, [&](const document_match& match)
            {
                return !request.filter.matches(match.value.metadata);
            });
    co_return matches;
}

functional_retriever::functional_retriever(retrieval_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("retrieval handler cannot be empty");
}

auto functional_retriever::retrieve(std::string query, std::size_t limit,
    float minimum_score)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    co_return co_await search({.query = std::move(query),
        .limit = limit,
        .minimum_score = minimum_score});
}

auto functional_retriever::search(retrieval_request request)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    if (request.config && request.config->is_cancelled())
        co_return std::unexpected("retrieval cancelled");
    co_return co_await handler_(std::move(request));
}

delegating_embedding_store::delegating_embedding_store(
    embedding_store_handlers handlers)
    : handlers_(std::move(handlers))
{
    if (!handlers_.search)
        throw std::invalid_argument(
            "embedding store search handler cannot be empty");
}

auto delegating_embedding_store::add_documents(
    std::vector<document> documents)
    -> task<std::expected<void, std::string>>
{
    if (!handlers_.add)
        co_return std::unexpected(
            "embedding store does not support document insertion");
    co_return co_await handlers_.add(std::move(documents));
}

auto delegating_embedding_store::retrieve(std::string query,
    std::size_t limit, float minimum_score)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    co_return co_await search({.query = std::move(query),
        .limit = limit,
        .minimum_score = minimum_score});
}

auto delegating_embedding_store::search(retrieval_request request)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    if (request.config && request.config->is_cancelled())
        co_return std::unexpected("embedding store search cancelled");
    co_return co_await handlers_.search(std::move(request));
}

auto delegating_embedding_store::remove_documents(
    std::vector<std::string> ids)
    -> task<std::expected<std::size_t, std::string>>
{
    if (!handlers_.remove_ids)
        co_return std::unexpected(
            "embedding store does not support removal by id");
    co_return co_await handlers_.remove_ids(std::move(ids));
}

auto delegating_embedding_store::remove_documents(metadata_filter filter)
    -> task<std::expected<std::size_t, std::string>>
{
    if (!handlers_.remove_filter)
        co_return std::unexpected(
            "embedding store does not support filtered removal");
    co_return co_await handlers_.remove_filter(std::move(filter));
}

auto delegating_embedding_store::clear()
    -> task<std::expected<void, std::string>>
{
    if (!handlers_.clear)
        co_return std::unexpected("embedding store does not support clearing");
    co_return co_await handlers_.clear();
}

auto delegating_embedding_store::size()
    -> task<std::expected<std::size_t, std::string>>
{
    if (!handlers_.size)
        co_return std::unexpected(
            "embedding store does not support counting");
    co_return co_await handlers_.size();
}

in_memory_vector_store::in_memory_vector_store(io_context& context,
    thread_pool& pool, embedding_model& embeddings)
    : context_(context), pool_(pool), embeddings_(embeddings)
{
}

auto in_memory_vector_store::add_documents(std::vector<document> documents)
    -> task<std::expected<void, std::string>>
{
    if (documents.empty())
        co_return std::expected<void, std::string>{};
    std::vector<std::string> texts;
    texts.reserve(documents.size());
    for (const auto& item : documents)
        texts.push_back(item.page_content);
    auto vectors = co_await embeddings_.embed_documents(std::move(texts));
    if (!vectors)
        co_return std::unexpected(vectors.error());
    if (vectors->size() != documents.size())
        co_return std::unexpected("embedding count does not match document count");

    std::vector<entry> additions;
    additions.reserve(documents.size());
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        if ((*vectors)[index].empty())
            co_return std::unexpected("embedding vectors cannot be empty");
        additions.push_back({std::move(documents[index]),
            normalize(std::move((*vectors)[index]))});
    }
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto dimensions = additions.front().embedding.size();
    if (dimensions_ && *dimensions_ != dimensions)
        co_return std::unexpected("embedding dimensions do not match vector store");
    if (std::ranges::any_of(additions, [dimensions](const entry& item)
            {
                return item.embedding.size() != dimensions;
            }))
        co_return std::unexpected("embedding dimensions are inconsistent");
    dimensions_ = dimensions;
    for (auto& addition : additions)
    {
        const auto existing = std::ranges::find(entries_, addition.value.id,
            [](const entry& item)
            {
                return item.value.id;
            });
        if (!addition.value.id.empty() && existing != entries_.end())
            *existing = std::move(addition);
        else
            entries_.push_back(std::move(addition));
    }
    co_return std::expected<void, std::string>{};
}

auto in_memory_vector_store::retrieve(std::string query, std::size_t limit,
    float minimum_score)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    co_return co_await search({.query = std::move(query),
        .limit = limit,
        .minimum_score = minimum_score});
}

auto in_memory_vector_store::search(retrieval_request request)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    if (request.config && request.config->is_cancelled())
        co_return std::unexpected("vector search cancelled");
    auto query_vector = co_await embeddings_.embed_query(
        std::move(request.query));
    if (!query_vector)
        co_return std::unexpected(query_vector.error());
    if (request.config && request.config->is_cancelled())
        co_return std::unexpected("vector search cancelled");
    auto normalized_query = normalize(std::move(*query_vector));

    co_await mutex_.lock();
    std::vector<entry> snapshot;
    {
        async_lock_guard guard(mutex_, std::adopt_lock);
        if (dimensions_ && normalized_query.size() != *dimensions_)
            co_return std::unexpected("query embedding dimensions do not match vector store");
        snapshot = entries_;
    }
    auto matches = co_await blocking_invoke(pool_, context_,
        [snapshot = std::move(snapshot), query = std::move(normalized_query),
            request = std::move(request)]() mutable
        {
            std::vector<document_match> result;
            for (auto& item : snapshot)
            {
                if (!request.filter.matches(item.value.metadata))
                    continue;
                const auto score = similarity(query, item.embedding);
                if (score >= request.minimum_score)
                    result.push_back({std::move(item.value), score});
            }
            std::ranges::sort(result, std::greater{}, &document_match::score);
            if (result.size() > request.limit)
                result.resize(request.limit);
            return result;
        });
    co_return matches;
}

auto in_memory_vector_store::remove_documents(std::vector<std::string> ids)
    -> task<std::expected<std::size_t, std::string>>
{
    std::unordered_set<std::string> selected{
        std::make_move_iterator(ids.begin()),
        std::make_move_iterator(ids.end())};
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto before = entries_.size();
    std::erase_if(entries_, [&](const entry& item)
        {
            return selected.contains(item.value.id);
        });
    if (entries_.empty())
        dimensions_.reset();
    co_return before - entries_.size();
}

auto in_memory_vector_store::remove_documents(metadata_filter filter)
    -> task<std::expected<std::size_t, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto before = entries_.size();
    std::erase_if(entries_, [&](const entry& item)
        {
            return filter.matches(item.value.metadata);
        });
    if (entries_.empty())
        dimensions_.reset();
    co_return before - entries_.size();
}

auto in_memory_vector_store::clear()
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    entries_.clear();
    dimensions_.reset();
    co_return std::expected<void, std::string>{};
}

auto in_memory_vector_store::size()
    -> task<std::expected<std::size_t, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return std::expected<std::size_t, std::string>{entries_.size()};
}

retrieval_chain::retrieval_chain(retriever& source, chat_model& model,
    chat_prompt_template prompt, retrieval_chain_options options)
    : source_(source), model_(model), prompt_(std::move(prompt)), options_(std::move(options))
{
}

auto retrieval_chain::invoke(std::string input, chat_request defaults,
    const run_config& config)
    -> task<std::expected<retrieval_result, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("retrieval invocation cancelled");
    run_scope retrieval_run{config, run_event_type::retriever_start,
        run_event_type::retriever_end, run_event_type::retriever_error,
        "retriever", input};
    auto documents = co_await source_.retrieve(input, options_.limit,
        options_.minimum_score);
    if (!documents)
        co_return std::unexpected(documents.error());
    retrieval_run.succeed(std::format("{} documents", documents->size()), 0,
        {{"document_count", documents->size()}});

    std::string context;
    for (const auto& match : *documents)
    {
        if (!context.empty())
            context += options_.document_separator;
        context += match.value.page_content;
    }
    auto messages = prompt_.format({{options_.input_variable, input},
        {options_.context_variable, std::move(context)}});
    if (!messages)
        co_return std::unexpected(messages.error());
    defaults.messages.insert(defaults.messages.end(),
        std::make_move_iterator(messages->begin()),
        std::make_move_iterator(messages->end()));
    auto response = co_await model_.invoke(std::move(defaults), config);
    if (!response)
        co_return std::unexpected(response.error());
    if (response->choices.empty())
        co_return std::unexpected("model returned no choices");
    co_return retrieval_result{.output = std::move(response->choices.front().msg),
        .documents = std::move(*documents),
        .token_usage = response->token_usage};
}

} // namespace cnetmod::openai
