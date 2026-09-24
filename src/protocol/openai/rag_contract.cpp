/// cnetmod.protocol.openai:rag — typed model response contracts

module;

#include <glaze/json/read.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.json;
import :foundation;

namespace cnetmod::openai::detail {

struct queries_response
{
    std::vector<std::string> queries;
};

struct routes_response
{
    std::vector<std::string> routes;
};

struct scored_document
{
    std::size_t index = 0;
    double score = 0.0;
};

struct scores_response
{
    std::vector<scored_document> scores;
};

auto queries_schema() -> json
{
    auto schema = cnetmod::json::object();
    schema["type"] = "object";
    schema["properties"] = cnetmod::json::object();
    auto& queries = schema["properties"]["queries"];
    queries = cnetmod::json::object();
    queries["type"] = "array";
    queries["items"] = cnetmod::json::object();
    queries["items"]["type"] = "string";
    schema["required"] = cnetmod::json::array({"queries"});
    schema["additionalProperties"] = false;
    return schema;
}

auto routes_schema(const json& names) -> json
{
    auto schema = cnetmod::json::object();
    schema["type"] = "object";
    schema["properties"] = cnetmod::json::object();
    auto& routes = schema["properties"]["routes"];
    routes = cnetmod::json::object();
    routes["type"] = "array";
    routes["items"] = cnetmod::json::object();
    routes["items"]["type"] = "string";
    routes["items"]["enum"] = names;
    routes["uniqueItems"] = true;
    schema["required"] = cnetmod::json::array({"routes"});
    schema["additionalProperties"] = false;
    return schema;
}

auto scores_schema() -> json
{
    auto schema = cnetmod::json::object();
    schema["type"] = "object";
    schema["properties"] = cnetmod::json::object();
    auto& scores = schema["properties"]["scores"];
    scores = cnetmod::json::object();
    scores["type"] = "array";
    auto& item = scores["items"];
    item = cnetmod::json::object();
    item["type"] = "object";
    item["properties"] = cnetmod::json::object();
    item["properties"]["index"] = cnetmod::json::object();
    item["properties"]["index"]["type"] = "integer";
    auto& score = item["properties"]["score"];
    score = cnetmod::json::object();
    score["type"] = "number";
    score["minimum"] = 0.0;
    score["maximum"] = 1.0;
    item["required"] = cnetmod::json::array({"index", "score"});
    item["additionalProperties"] = false;
    schema["required"] = cnetmod::json::array({"scores"});
    schema["additionalProperties"] = false;
    return schema;
}

auto parse_queries(std::string_view text)
    -> std::expected<std::vector<std::string>, std::error_code>
{
    auto parsed = cnetmod::json::parse<queries_response>(text);
    if (!parsed)
        return std::unexpected(parsed.error());
    return std::move(parsed->queries);
}

auto parse_routes(std::string_view text)
    -> std::expected<std::vector<std::string>, std::error_code>
{
    auto parsed = cnetmod::json::parse<routes_response>(text);
    if (!parsed)
        return std::unexpected(parsed.error());
    return std::move(parsed->routes);
}

auto parse_scores(std::string_view text)
    -> std::expected<std::vector<std::pair<std::size_t, double>>, std::error_code>
{
    auto parsed = cnetmod::json::parse<scores_response>(text);
    if (!parsed)
        return std::unexpected(parsed.error());
    std::vector<std::pair<std::size_t, double>> result;
    result.reserve(parsed->scores.size());
    for (const auto& item : parsed->scores)
        result.emplace_back(item.index, item.score);
    return result;
}

} // namespace cnetmod::openai::detail
