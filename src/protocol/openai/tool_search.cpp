/// cnetmod.protocol.openai:tool_search — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :model;
import :tools;
import :tool_search;

namespace cnetmod::openai {

namespace {
    auto normalized_tokens(std::string_view value) -> std::set<std::string>
    {
        std::set<std::string> result;
        std::string token;
        for (const auto character : value)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (std::isalnum(byte) || character == '_')
                token.push_back(static_cast<char>(std::tolower(byte)));
            else if (!token.empty())
            {
                result.insert(std::move(token));
                token.clear();
            }
        }
        if (!token.empty())
            result.insert(std::move(token));
        return result;
    }

    auto searchable_text(const tool& candidate) -> std::string
    {
        return std::format("{} {} {}", candidate.function_name,
            candidate.function_description, candidate.function_parameters.dump());
    }

    auto cosine_similarity(const std::vector<float>& left,
        const std::vector<float>& right) -> std::expected<float, std::string>
    {
        if (left.empty() || left.size() != right.size())
            return std::unexpected("tool-search embeddings have incompatible dimensions");
        double dot = 0.0;
        double left_norm = 0.0;
        double right_norm = 0.0;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            dot += static_cast<double>(left[index]) * right[index];
            left_norm += static_cast<double>(left[index]) * left[index];
            right_norm += static_cast<double>(right[index]) * right[index];
        }
        if (left_norm <= 0.0 || right_norm <= 0.0)
            return 0.0F;
        return static_cast<float>(dot / std::sqrt(left_norm * right_norm));
    }

    void limit_matches(std::vector<tool_search_match>& matches,
        std::size_t max_results)
    {
        std::ranges::sort(matches, [](const auto& left, const auto& right)
            {
                if (left.score != right.score)
                    return left.score > right.score;
                return left.name < right.name;
            });
        if (matches.size() > max_results)
            matches.resize(max_results);
    }
} // namespace

auto keyword_tool_search::search(tool_search_request request,
    const run_config& config)
    -> task<std::expected<std::vector<tool_search_match>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("tool search cancelled");
    if (request.query.empty())
        co_return std::unexpected("tool search query cannot be empty");
    const auto query_tokens = normalized_tokens(request.query);
    std::vector<tool_search_match> matches;
    for (const auto& candidate : request.candidates)
    {
        auto candidate_text = searchable_text(candidate);
        auto candidate_tokens = normalized_tokens(candidate_text);
        std::size_t overlap = 0;
        for (const auto& token : query_tokens)
            overlap += candidate_tokens.contains(token) ? 1U : 0U;
        auto lowered_name = candidate.function_name;
        std::ranges::transform(lowered_name, lowered_name.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
        auto lowered_query = request.query;
        std::ranges::transform(lowered_query, lowered_query.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
        const bool name_match = lowered_name.contains(lowered_query) ||
            lowered_query.contains(lowered_name);
        if (overlap > 0 || name_match)
            matches.push_back({candidate.function_name,
                static_cast<float>(overlap) + (name_match ? 2.0F : 0.0F)});
    }
    limit_matches(matches, request.max_results);
    co_return matches;
}

semantic_tool_search::semantic_tool_search(embedding_model& embeddings) noexcept
    : embeddings_(embeddings)
{
}

auto semantic_tool_search::search(tool_search_request request,
    const run_config& config)
    -> task<std::expected<std::vector<tool_search_match>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("tool search cancelled");
    if (request.query.empty())
        co_return std::unexpected("tool search query cannot be empty");
    if (request.candidates.empty() || request.max_results == 0)
        co_return std::vector<tool_search_match>{};

    std::vector<std::string> descriptions;
    descriptions.reserve(request.candidates.size());
    for (const auto& candidate : request.candidates)
        descriptions.push_back(searchable_text(candidate));
    auto vectors = co_await embeddings_.embed_documents(std::move(descriptions));
    if (!vectors)
        co_return std::unexpected("tool description embedding failed: " +
            vectors.error());
    auto query = co_await embeddings_.embed_query(std::move(request.query));
    if (!query)
        co_return std::unexpected("tool query embedding failed: " + query.error());
    if (vectors->size() != request.candidates.size())
        co_return std::unexpected("embedding model returned the wrong tool count");

    std::vector<tool_search_match> matches;
    matches.reserve(request.candidates.size());
    for (std::size_t index = 0; index < request.candidates.size(); ++index)
    {
        auto score = cosine_similarity((*vectors)[index], *query);
        if (!score)
            co_return std::unexpected(score.error());
        matches.push_back({request.candidates[index].function_name, *score});
    }
    limit_matches(matches, request.max_results);
    co_return matches;
}

} // namespace cnetmod::openai
