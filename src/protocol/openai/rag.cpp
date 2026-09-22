/// cnetmod.protocol.openai:rag — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.task_group;
import cnetmod.coro.cancel;
import cnetmod.io.io_context;
import :foundation;
import :messages;
import :model;
import :prompt;
import :retrieval;
import :filters;
import :rag;

namespace cnetmod::openai {

functional_query_transformer::functional_query_transformer(
    query_transform_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("query transformer handler cannot be empty");
}

auto functional_query_transformer::transform(retrieval_query query,
    const run_config& config)
    -> task<std::expected<std::vector<retrieval_query>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("query transformation cancelled");
    co_return co_await handler_(std::move(query), config);
}

model_query_transformer::model_query_transformer(chat_model& model,
    model_query_transformer_options options)
    : model_(model), options_(std::move(options))
{
    if (options_.max_queries == 0 || options_.max_queries > 32)
        throw std::invalid_argument(
            "model query transformer max_queries must be between 1 and 32");
}

auto model_query_transformer::transform(retrieval_query query,
    const run_config& config)
    -> task<std::expected<std::vector<retrieval_query>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("query transformation cancelled");
    const auto maximum = options_.strategy == query_transformation::expand
        ? options_.max_queries
        : std::size_t{1};
    std::string instruction;
    std::string strategy;
    switch (options_.strategy)
    {
    case query_transformation::compress:
        strategy = "compress";
        instruction = "Compress the input into one concise standalone search query. " "Preserve every constraint and named entity.";
        break;
    case query_transformation::rewrite:
        strategy = "rewrite";
        instruction = "Rewrite the input into one precise standalone search query. " "Resolve ambiguity without inventing facts.";
        break;
    case query_transformation::expand:
        strategy = "expand";
        instruction = std::format(
            "Produce between 1 and {} distinct search queries covering different " "relevant terminology and perspectives.", maximum);
        break;
    case query_transformation::hypothetical_document:
        strategy = "hypothetical_document";
        instruction = "Write one short hypothetical passage that would directly answer " "the input. It will be embedded for retrieval, not shown as fact.";
        break;
    }
    const json schema{{"type", "object"},
        {"properties", {{"queries", {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
        {"required", {"queries"}}, {"additionalProperties", false}};
    chat_request request;
    request.model = options_.model;
    request.temperature = 0.0;
    request.response_format = "json_schema";
    request.response_schema_name = "transformed_retrieval_queries";
    request.response_schema = schema;
    request.response_schema_strict = true;
    request.messages = {message::system(instruction +
                            " Return only the requested structured result."),
        message::user(cnetmod::json::write_document(
                          cnetmod::json::object({{"query", query.text},
                              {"metadata", query.metadata}}))
                          .value_or("{}"))};
    auto response = co_await model_.invoke(std::move(request), config);
    if (!response)
        co_return std::unexpected(
            "model query transformation failed: " + response.error());
    if (response->choices.empty())
        co_return std::unexpected(
            "model query transformer returned no choices");
    auto parsed_transformed = cnetmod::json::parse_document(
        response->choices.front().msg.content);
    if (!parsed_transformed)
        co_return std::unexpected(
            "model query transformer returned invalid JSON");
    const auto& transformed = *parsed_transformed;
    auto valid = validate_json_schema(transformed, schema);
    if (!valid)
        co_return std::unexpected(
            "model query transformer response validation failed: " +
            valid.error());
    const auto& values = transformed["queries"].get_array();
    if (values.empty() || values.size() > maximum)
        co_return std::unexpected(std::format(
            "model query transformer returned {} queries; expected 1..{}",
            values.size(), maximum));

    std::vector<retrieval_query> result;
    result.reserve(values.size() + (options_.include_original ? 1 : 0));
    std::set<std::string, std::less<>> unique;
    if (options_.include_original && !query.text.empty())
    {
        unique.insert(query.text);
        result.push_back(query);
        result.back().metadata["transformation"] = "original";
    }
    for (const auto& value : values)
    {
        if (!value.is_string())
            co_return std::unexpected(
                "model query transformer returned a non-string query");
        auto text = value.get<std::string>();
        const auto first = text.find_first_not_of(" \t\r\n");
        const auto last = text.find_last_not_of(" \t\r\n");
        if (first == std::string::npos)
            co_return std::unexpected(
                "model query transformer returned a blank query");
        text = text.substr(first, last - first + 1);
        if (!unique.insert(text).second)
            continue;
        auto item = query;
        item.text = std::move(text);
        item.metadata["transformation"] = strategy;
        result.push_back(std::move(item));
    }
    if (result.empty())
        co_return std::unexpected(
            "model query transformer returned no unique queries");
    co_return result;
}

functional_query_router::functional_query_router(query_route_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("query router handler cannot be empty");
}

auto functional_query_router::route(const retrieval_query& query,
    const run_config& config)
    -> task<std::expected<std::vector<retriever*>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("query routing cancelled");
    co_return co_await handler_(query, config);
}

functional_content_aggregator::functional_content_aggregator(
    content_aggregate_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("content aggregator handler cannot be empty");
}

auto functional_content_aggregator::aggregate(
    std::vector<std::vector<document_match>> ranked_lists,
    std::size_t limit) -> std::vector<document_match>
{
    return handler_(std::move(ranked_lists), limit);
}

functional_content_reranker::functional_content_reranker(
    content_rerank_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("content reranker handler cannot be empty");
}

auto functional_content_reranker::rerank(std::string query,
    std::vector<document_match> documents, const run_config& config)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("content reranking cancelled");
    co_return co_await handler_(std::move(query), std::move(documents), config);
}

functional_context_injector::functional_context_injector(
    context_injection_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("context injector handler cannot be empty");
}

auto functional_context_injector::inject(message input,
    const std::vector<document_match>& documents) -> std::vector<message>
{
    return handler_(std::move(input), documents);
}

auto identity_query_transformer::transform(retrieval_query query,
    const run_config&) -> task<std::expected<std::vector<retrieval_query>, std::string>>
{
    co_return std::vector<retrieval_query>{std::move(query)};
}

static_query_router::static_query_router(std::vector<retriever*> retrievers)
    : retrievers_(std::move(retrievers))
{
}

auto static_query_router::route(const retrieval_query&, const run_config&)
    -> task<std::expected<std::vector<retriever*>, std::string>>
{
    if (retrievers_.empty())
        co_return std::unexpected("query router has no retrievers");
    if (std::ranges::any_of(retrievers_, [](const retriever* value)
            {
                return value == nullptr;
            }))
        co_return std::unexpected("query router contains a null retriever");
    co_return retrievers_;
}

model_query_router::model_query_router(chat_model& model,
    std::vector<named_retriever> retrievers,
    model_query_router_options options)
    : model_(model), retrievers_(std::move(retrievers)), options_(std::move(options))
{
    if (retrievers_.empty())
        throw std::invalid_argument("model query router has no retrievers");
    std::set<std::string, std::less<>> names;
    for (const auto& candidate : retrievers_)
    {
        if (candidate.name.empty())
            throw std::invalid_argument("retriever name cannot be empty");
        if (!candidate.source)
            throw std::invalid_argument("named retriever cannot be null");
        if (!names.insert(candidate.name).second)
            throw std::invalid_argument(
                "duplicate retriever name: " + candidate.name);
    }
}

auto model_query_router::fallback(std::string error)
    -> std::expected<std::vector<retriever*>, std::string>
{
    switch (options_.fallback)
    {
    case retrieval_route_fallback::none:
        return std::vector<retriever*>{};
    case retrieval_route_fallback::all:
    {
        std::vector<retriever*> result;
        result.reserve(retrievers_.size());
        for (const auto& candidate : retrievers_)
            result.push_back(candidate.source);
        return result;
    }
    case retrieval_route_fallback::fail:
        return std::unexpected(std::move(error));
    }
    return std::unexpected(std::move(error));
}

auto model_query_router::route(const retrieval_query& query,
    const run_config& config)
    -> task<std::expected<std::vector<retriever*>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("query routing cancelled");
    json candidates = cnetmod::json::array();
    json names = cnetmod::json::array();
    for (const auto& candidate : retrievers_)
    {
        candidates.get_array().push_back(cnetmod::json::object(
            {{"name", candidate.name},
                {"description", candidate.description}}));
        names.get_array().emplace_back(candidate.name);
    }
    const json schema{{"type", "object"},
        {"properties",
            {{"routes", {{"type", "array"}, {"items", {{"type", "string"}, {"enum", names}}}, {"uniqueItems", true}}}}},
        {"required", {"routes"}}, {"additionalProperties", false}};
    chat_request request;
    if (!options_.model.empty())
        request.model = options_.model;
    request.temperature = 0.0;
    request.response_format = "json_schema";
    request.response_schema_name = "retrieval_routes";
    request.response_schema = schema;
    request.response_schema_strict = true;
    request.messages = {message::system(
                            "Select every retriever relevant to the query. " "Return only the requested structured result."),
        message::user(cnetmod::json::write_document(cnetmod::json::object(
                          {{"query", query.text},
                              {"query_metadata", query.metadata},
                              {"retrievers", candidates}}))
                          .value_or("{}"))};
    auto response = co_await model_.invoke(std::move(request), config);
    if (!response)
        co_return fallback("model query routing failed: " + response.error());
    if (response->choices.empty())
        co_return fallback("model query router returned no choices");
    auto parsed_selection = cnetmod::json::parse_document(
        response->choices.front().msg.content);
    if (!parsed_selection)
        co_return fallback("model query router returned invalid JSON");
    const auto& selection = *parsed_selection;
    auto valid = validate_json_schema(selection, schema);
    if (!valid)
        co_return fallback(
            "model query router response validation failed: " +
            valid.error());

    std::vector<retriever*> result;
    std::set<std::string, std::less<>> selected_names;
    for (const auto& name : selection["routes"].get_array())
    {
        const auto value = name.get<std::string>();
        if (!selected_names.insert(value).second)
            co_return fallback(
                "model query router selected a retriever more than once");
        const auto found = std::ranges::find(retrievers_, value,
            &named_retriever::name);
        if (found == retrievers_.end())
            co_return fallback("model query router selected an unknown retriever");
        result.push_back(found->source);
    }
    co_return result;
}

reciprocal_rank_fusion::reciprocal_rank_fusion(float rank_constant)
    : rank_constant_(std::max(1.0F, rank_constant))
{
}

auto reciprocal_rank_fusion::aggregate(
    std::vector<std::vector<document_match>> ranked_lists, std::size_t limit)
    -> std::vector<document_match>
{
    struct fused_entry
    {
        document value;
        float score = 0.0F;
    };

    std::map<std::string, fused_entry, std::less<>> fused;
    for (auto& ranked : ranked_lists)
    {
        for (std::size_t rank = 0; rank < ranked.size(); ++rank)
        {
            auto& match = ranked[rank];
            auto key = match.value.id.empty() ? match.value.page_content
                                              : match.value.id;
            auto [position, inserted] = fused.try_emplace(
                std::move(key), fused_entry{.value = std::move(match.value)});
            if (!inserted && position->second.value.page_content.empty())
                position->second.value = std::move(match.value);
            position->second.score +=
                1.0F / (rank_constant_ + static_cast<float>(rank + 1));
        }
    }

    std::vector<document_match> result;
    result.reserve(fused.size());
    for (auto& [key, value] : fused)
        result.push_back({.value = std::move(value.value), .score = value.score});
    std::ranges::sort(result, [](const document_match& left, const document_match& right)
        {
            if (left.score != right.score)
                return left.score > right.score;
            return left.value.id < right.value.id;
        });
    if (limit > 0 && result.size() > limit)
        result.resize(limit);
    return result;
}

functional_scoring_model::functional_scoring_model(scoring_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("scoring handler cannot be empty");
}

auto functional_scoring_model::score(std::string query,
    const std::vector<document>& documents, const run_config& config)
    -> task<std::expected<std::vector<float>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document scoring cancelled");
    co_return co_await handler_(std::move(query), documents, config);
}

chat_scoring_model::chat_scoring_model(chat_model& model,
    chat_scoring_options options)
    : model_(model), options_(std::move(options))
{
    if (options_.max_document_characters == 0)
        throw std::invalid_argument(
            "scoring document character limit cannot be zero");
}

auto chat_scoring_model::score(std::string query,
    const std::vector<document>& documents, const run_config& config)
    -> task<std::expected<std::vector<float>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document scoring cancelled");
    if (documents.empty())
        co_return std::vector<float>{};

    json candidates = cnetmod::json::array();
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        const auto& candidate = documents[index];
        candidates.get_array().push_back(cnetmod::json::object(
            {{"index", index}, {"id", candidate.id},
                {"content", candidate.page_content.substr(
                                0, options_.max_document_characters)}}));
    }
    const json schema{{"type", "object"},
        {"properties",
            {{"scores",
                {{"type", "array"},
                    {"items",
                        {{"type", "object"},
                            {"properties",
                                {{"index", {{"type", "integer"}}},
                                    {"score", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}}},
                            {"required", {"index", "score"}},
                            {"additionalProperties", false}}}}}}},
        {"required", {"scores"}}, {"additionalProperties", false}};
    chat_request request;
    if (!options_.model.empty())
        request.model = options_.model;
    request.temperature = 0.0;
    request.response_format = "json_schema";
    request.response_schema_name = "relevance_scores";
    request.response_schema = schema;
    request.response_schema_strict = true;
    request.messages = {message::system(
                            "Score every candidate's relevance to the query " "from 0 to 1. Return each index exactly once."),
        message::user(cnetmod::json::write_document(cnetmod::json::object(
                          {{"query", query}, {"candidates", candidates}}))
                          .value_or("{}"))};
    auto response = co_await model_.invoke(std::move(request), config);
    if (!response)
        co_return std::unexpected("document scoring failed: " +
            response.error());
    if (response->choices.empty())
        co_return std::unexpected("document scoring returned no choices");
    auto parsed_result = cnetmod::json::parse_document(
        response->choices.front().msg.content);
    if (!parsed_result)
        co_return std::unexpected("document scoring returned invalid JSON");
    const auto& result = *parsed_result;
    auto valid = validate_json_schema(result, schema);
    if (!valid)
        co_return std::unexpected(
            "document scoring response validation failed: " + valid.error());

    std::vector<float> scores(documents.size());
    std::vector<bool> assigned(documents.size(), false);
    for (const auto& item : result["scores"].get_array())
    {
        const auto index = item["index"].as<std::size_t>();
        const auto value = item["score"].as<float>();
        if (index >= documents.size() || assigned[index] ||
            !std::isfinite(value) || value < 0.0F || value > 1.0F)
            co_return std::unexpected(
                "document scoring returned invalid index or score");
        scores[index] = value;
        assigned[index] = true;
    }
    if (!std::ranges::all_of(assigned, std::identity{}))
        co_return std::unexpected(
            "document scoring did not score every candidate");
    co_return scores;
}

scoring_reranker::scoring_reranker(scoring_model& model,
    float minimum_score) noexcept
    : model_(model), minimum_score_(minimum_score)
{
}

auto scoring_reranker::rerank(std::string query,
    std::vector<document_match> documents, const run_config& config)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    std::vector<document> candidates;
    candidates.reserve(documents.size());
    for (const auto& match : documents)
        candidates.push_back(match.value);
    auto scores = co_await model_.score(
        std::move(query), candidates, config);
    if (!scores)
        co_return std::unexpected(scores.error());
    if (scores->size() != documents.size())
        co_return std::unexpected(
            "scoring model returned an unexpected score count");
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        if (!std::isfinite((*scores)[index]))
            co_return std::unexpected("scoring model returned a non-finite score");
        documents[index].score = (*scores)[index];
    }
    std::erase_if(documents, [&](const auto& match)
        {
            return match.score < minimum_score_;
        });
    std::ranges::stable_sort(documents, std::greater{},
        &document_match::score);
    co_return documents;
}

auto passthrough_reranker::rerank(std::string,
    std::vector<document_match> documents, const run_config&)
    -> task<std::expected<std::vector<document_match>, std::string>>
{
    co_return documents;
}

developer_context_injector::developer_context_injector(
    std::string instruction, std::string separator)
    : instruction_(std::move(instruction)), separator_(std::move(separator))
{
}

auto developer_context_injector::inject(message input,
    const std::vector<document_match>& documents) -> std::vector<message>
{
    std::string context;
    for (const auto& match : documents)
    {
        if (!context.empty())
            context += separator_;
        context += match.value.page_content;
    }
    auto instruction = instruction_;
    if (const auto marker = instruction.find("{context}");
        marker != std::string::npos)
        instruction.replace(marker, std::string_view{"{context}"}.size(), context);
    return {message::developer(instruction), std::move(input)};
}

citation_context_injector::citation_context_injector(
    citation_context_options options)
    : options_(std::move(options))
{
}

auto citation_context_injector::inject(message input,
    const std::vector<document_match>& documents) -> std::vector<message>
{
    std::string context;
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        if (!context.empty())
            context += options_.separator;
        const auto& match = documents[index];
        context += "[source ";
        context += std::to_string(index + 1);
        context += ']';
        if (options_.include_document_id && !match.value.id.empty())
            context += " id=" + match.value.id;
        if (options_.include_score)
            context += " score=" + std::format("{:.6g}", match.score);
        for (const auto& field : options_.metadata_fields)
        {
            const auto* entry = cnetmod::json::find(
                match.value.metadata, field);
            if (entry == nullptr || entry->is_null())
                continue;
            context += ' ' + field + '=';
            context += entry->is_string() ? entry->get<std::string>()
                                          : cnetmod::json::write_document(*entry)
                                                .value_or("null");
        }
        context += '\n';
        context += match.value.page_content;
    }
    auto instruction = options_.instruction;
    if (const auto marker = instruction.find("{context}");
        marker != std::string::npos)
        instruction.replace(marker, std::string_view{"{context}"}.size(),
            context);
    else if (!context.empty())
        instruction += options_.separator + context;
    return {message::developer(std::move(instruction)), std::move(input)};
}

retrieval_augmentor::retrieval_augmentor(query_transformer& transformer,
    query_router& router, content_aggregator& aggregator,
    context_injector& injector, content_reranker* reranker)
    : transformer_(transformer), router_(router), aggregator_(aggregator), injector_(injector), reranker_(reranker)
{
}

retrieval_augmentor::retrieval_augmentor(io_context& context,
    query_transformer& transformer, query_router& router,
    content_aggregator& aggregator, context_injector& injector,
    content_reranker* reranker)
    : transformer_(transformer), router_(router), aggregator_(aggregator), injector_(injector), reranker_(reranker), parallel_context_(&context)
{
}

auto retrieval_augmentor::augment(message input, retrieval_query query,
    const run_config& config)
    -> task<std::expected<retrieval_augmentation, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("retrieval augmentation cancelled");
    run_scope retrieval_run{config, run_event_type::retriever_start,
        run_event_type::retriever_end, run_event_type::retriever_error,
        "retrieval_augmentor", query.text};
    auto queries = co_await transformer_.transform(std::move(query), config);
    if (!queries)
        co_return std::unexpected("query transformation failed: " + queries.error());
    if (queries->empty())
        co_return std::unexpected("query transformer produced no queries");

    struct retrieval_job
    {
        retrieval_query query;
        retriever* source = nullptr;
    };

    std::vector<retrieval_job> jobs;
    const auto limit = queries->front().limit;
    for (const auto& transformed : *queries)
    {
        if (config.is_cancelled())
            co_return std::unexpected("retrieval augmentation cancelled");
        auto routes = co_await router_.route(transformed, config);
        if (!routes)
            co_return std::unexpected("query routing failed: " + routes.error());
        for (auto* source : *routes)
        {
            if (!source)
                co_return std::unexpected(
                    "query router returned a null retriever");
            jobs.push_back({transformed, source});
        }
    }
    std::vector<std::optional<std::expected<std::vector<document_match>,
        std::string>>>
        results(jobs.size());
    if (parallel_context_ && jobs.size() > 1)
    {
        task_group group{*parallel_context_};
        for (std::size_t index = 0; index < jobs.size(); ++index)
        {
            const auto started = group.run([&, index](cancel_token& cancellation)
                                               -> task<std::expected<void, std::error_code>>
                {
                    if (config.is_cancelled())
                    {
                        results[index].emplace(std::unexpected(
                            "retrieval augmentation cancelled"));
                        co_return std::unexpected(
                            std::make_error_code(std::errc::operation_canceled));
                    }
                    auto child_config = config;
                    child_config.cancellation = &cancellation;
                    const auto& job = jobs[index];
                    results[index].emplace(co_await job.source->search(
                        {.query = job.query.text,
                            .limit = job.query.limit,
                            .minimum_score = job.query.minimum_score,
                            .filter = job.query.filter,
                            .config = &child_config}));
                    if (!*results[index])
                        co_return std::unexpected(
                            std::make_error_code(std::errc::io_error));
                    co_return std::expected<void, std::error_code>{};
                });
            if (!started)
                co_return std::unexpected(
                    "parallel retrieval rejected a child task");
        }
        (void)co_await group.join();
    }
    else
    {
        for (std::size_t index = 0; index < jobs.size(); ++index)
        {
            const auto& job = jobs[index];
            results[index].emplace(co_await job.source->search(
                {.query = job.query.text,
                    .limit = job.query.limit,
                    .minimum_score = job.query.minimum_score,
                    .filter = job.query.filter,
                    .config = &config}));
        }
    }
    std::vector<std::vector<document_match>> ranked_lists;
    ranked_lists.reserve(results.size());
    for (const auto& result : results)
    {
        if (result && !*result)
            co_return std::unexpected(
                "content retrieval failed: " + result->error());
    }
    for (auto& result : results)
    {
        if (!result)
            co_return std::unexpected(
                "content retrieval was cancelled before completion");
        ranked_lists.push_back(std::move(**result));
    }

    auto documents = aggregator_.aggregate(std::move(ranked_lists), limit);
    if (reranker_)
    {
        auto reranked = co_await reranker_->rerank(
            queries->front().text, std::move(documents), config);
        if (!reranked)
            co_return std::unexpected("content reranking failed: " + reranked.error());
        documents = std::move(*reranked);
        if (limit > 0 && documents.size() > limit)
            documents.resize(limit);
    }
    retrieval_run.succeed(std::format("{} documents", documents.size()), 0,
        {{"document_count", documents.size()}, {"query_count", queries->size()}});
    auto messages = injector_.inject(std::move(input), documents);
    co_return retrieval_augmentation{.messages = std::move(messages),
        .documents = std::move(documents),
        .queries = std::move(*queries)};
}

} // namespace cnetmod::openai
