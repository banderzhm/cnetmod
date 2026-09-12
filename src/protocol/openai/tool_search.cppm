/// cnetmod.protocol.openai:tool_search — Context-efficient tool discovery

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:tool_search;

import std;
import cnetmod.coro.task;
import :model;
import :tools;

namespace cnetmod::openai {

export struct tool_search_request
{
    std::string query;
    std::vector<tool> candidates;
    std::size_t max_results = 5;
};

export struct tool_search_match
{
    std::string name;
    float score = 0.0F;
};

export class tool_search_strategy
{
public:
    virtual ~tool_search_strategy() = default;
    virtual auto search(tool_search_request request,
        const run_config& config = {})
        -> task<std::expected<std::vector<tool_search_match>, std::string>> = 0;
};

/// Lexical strategy using normalized name, description and schema tokens.
export class keyword_tool_search final : public tool_search_strategy
{
public:
    auto search(tool_search_request request,
        const run_config& config = {})
        -> task<std::expected<std::vector<tool_search_match>, std::string>> override;
};

/// Semantic strategy ranking tool descriptions with an embedding model.
export class semantic_tool_search final : public tool_search_strategy
{
public:
    explicit semantic_tool_search(embedding_model& embeddings) noexcept;

    auto search(tool_search_request request,
        const run_config& config = {})
        -> task<std::expected<std::vector<tool_search_match>, std::string>> override;

private:
    embedding_model& embeddings_;
};

} // namespace cnetmod::openai
