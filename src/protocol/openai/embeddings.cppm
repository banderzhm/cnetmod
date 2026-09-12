/// cnetmod.protocol.openai:embeddings — Embedding API wire contracts

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:embeddings;

import std;
import :foundation;

namespace cnetmod::openai {

export struct embedding_request
{
    std::string model = "text-embedding-3-small";
    std::vector<std::string> input;
    std::string encoding_format;
    std::optional<int> dimensions;
    std::string user;

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct embedding_data
{
    int index = 0;
    std::vector<float> embedding;
};

export struct embedding_response
{
    std::string model;
    std::vector<embedding_data> data;
    usage token_usage;

    [[nodiscard]] static auto from_json(std::string_view text) -> embedding_response;
};

} // namespace cnetmod::openai
