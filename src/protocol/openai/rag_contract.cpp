/// cnetmod.protocol.openai:rag — typed model response contracts

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

auto required_member(const json& source, std::string_view name,
    std::size_t expected_members) -> std::expected<const json*, std::error_code>
{
    if (!source.is_object())
        return std::unexpected(
            cnetmod::json::make_error_code(cnetmod::json::errc::type_mismatch));
    const auto* value = cnetmod::json::find(source, name);
    if (value == nullptr)
        return std::unexpected(
            cnetmod::json::make_error_code(cnetmod::json::errc::missing_field));
    if (source.get_object().size() != expected_members)
        return std::unexpected(
            cnetmod::json::make_error_code(cnetmod::json::errc::unknown_field));
    return value;
}

auto decode_string_array(const json& source, std::string_view name)
    -> std::expected<std::vector<std::string>, std::error_code>
{
    const auto member = required_member(source, name, 1);
    if (!member)
        return std::unexpected(member.error());
    if (!(*member)->is_array())
        return std::unexpected(
            cnetmod::json::make_error_code(cnetmod::json::errc::type_mismatch));

    std::vector<std::string> result;
    result.reserve((*member)->get_array().size());
    for (const auto& item : (*member)->get_array())
    {
        if (!item.is_string())
            return std::unexpected(cnetmod::json::make_error_code(
                cnetmod::json::errc::type_mismatch));
        result.push_back(item.get<std::string>());
    }
    return result;
}

auto decode_queries_response(const json& source)
    -> std::expected<queries_response, std::error_code>
{
    auto queries = decode_string_array(source, "queries");
    if (!queries)
        return std::unexpected(queries.error());
    return queries_response{std::move(*queries)};
}

auto decode_routes_response(const json& source)
    -> std::expected<routes_response, std::error_code>
{
    auto routes = decode_string_array(source, "routes");
    if (!routes)
        return std::unexpected(routes.error());
    return routes_response{std::move(*routes)};
}

auto decode_scores_response(const json& source)
    -> std::expected<scores_response, std::error_code>
{
    const auto member = required_member(source, "scores", 1);
    if (!member)
        return std::unexpected(member.error());
    if (!(*member)->is_array())
        return std::unexpected(
            cnetmod::json::make_error_code(cnetmod::json::errc::type_mismatch));

    scores_response result;
    result.scores.reserve((*member)->get_array().size());
    for (const auto& item : (*member)->get_array())
    {
        const auto index = required_member(item, "index", 2);
        const auto score = required_member(item, "score", 2);
        if (!index)
            return std::unexpected(index.error());
        if (!score)
            return std::unexpected(score.error());
        if (!(*index)->is_number() || !(*score)->is_number())
            return std::unexpected(cnetmod::json::make_error_code(
                cnetmod::json::errc::type_mismatch));
        try
        {
            result.scores.push_back(scored_document{
                .index = (*index)->as<std::size_t>(),
                .score = (*score)->as<double>(),
            });
        }
        catch (...)
        {
            return std::unexpected(cnetmod::json::make_error_code(
                cnetmod::json::errc::type_mismatch));
        }
    }
    return result;
}

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
    auto document = cnetmod::json::parse_document(text);
    if (!document)
        return std::unexpected(document.error());
    auto response = decode_queries_response(*document);
    if (!response)
        return std::unexpected(response.error());
    return std::move(response->queries);
}

auto parse_routes(std::string_view text)
    -> std::expected<std::vector<std::string>, std::error_code>
{
    auto document = cnetmod::json::parse_document(text);
    if (!document)
        return std::unexpected(document.error());
    auto response = decode_routes_response(*document);
    if (!response)
        return std::unexpected(response.error());
    return std::move(response->routes);
}

auto parse_scores(std::string_view text)
    -> std::expected<std::vector<std::pair<std::size_t, double>>, std::error_code>
{
    auto document = cnetmod::json::parse_document(text);
    if (!document)
        return std::unexpected(document.error());
    auto response = decode_scores_response(*document);
    if (!response)
        return std::unexpected(response.error());
    std::vector<std::pair<std::size_t, double>> result;
    result.reserve(response->scores.size());
    for (const auto& item : response->scores)
        result.emplace_back(item.index, item.score);
    return result;
}

} // namespace cnetmod::openai::detail
