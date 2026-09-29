/// cnetmod.protocol.openai:rag — typed model response contracts

module cnetmod.protocol.openai;

import std;
import cnetmod.json;
import :foundation;

namespace cnetmod::openai::detail {

namespace {

    [[nodiscard]] auto contract_error(cnetmod::json::errc value)
        -> std::unexpected<std::error_code>
    {
        return std::unexpected(cnetmod::json::make_error_code(value));
    }

    [[nodiscard]] auto string_array_member(
        const cnetmod::json::document& source, std::string_view name)
        -> std::expected<std::vector<std::string>, std::error_code>
    {
        if (!source.is_object())
            return contract_error(cnetmod::json::errc::type_mismatch);
        const auto* values = cnetmod::json::find(source, name);
        if (values == nullptr)
            return contract_error(cnetmod::json::errc::missing_field);
        if (source.size() != 1U)
            return contract_error(cnetmod::json::errc::unknown_field);
        if (!values->is_array())
            return contract_error(cnetmod::json::errc::type_mismatch);

        std::vector<std::string> result;
        result.reserve(values->size());
        for (const auto& value : values->get_array())
        {
            if (!value.is_string())
                return contract_error(cnetmod::json::errc::type_mismatch);
            result.push_back(value.get<std::string>());
        }
        return result;
    }

} // namespace

struct queries_response
{
    std::vector<std::string> queries;

    [[nodiscard]] static auto from_document(
        const cnetmod::json::document& source)
        -> std::expected<queries_response, std::error_code>
    {
        auto values = string_array_member(source, "queries");
        if (!values)
            return std::unexpected(values.error());
        return queries_response{.queries = std::move(*values)};
    }
};

struct routes_response
{
    std::vector<std::string> routes;

    [[nodiscard]] static auto from_document(
        const cnetmod::json::document& source)
        -> std::expected<routes_response, std::error_code>
    {
        auto values = string_array_member(source, "routes");
        if (!values)
            return std::unexpected(values.error());
        return routes_response{.routes = std::move(*values)};
    }
};

struct scored_document
{
    std::size_t index = 0;
    double score = 0.0;

    [[nodiscard]] static auto from_document(
        const cnetmod::json::document& source)
        -> std::expected<scored_document, std::error_code>
    {
        if (!source.is_object())
            return contract_error(cnetmod::json::errc::type_mismatch);
        const auto* index_value = cnetmod::json::find(source, "index");
        const auto* score_value = cnetmod::json::find(source, "score");
        if (index_value == nullptr || score_value == nullptr)
            return contract_error(cnetmod::json::errc::missing_field);
        if (source.size() != 2U)
            return contract_error(cnetmod::json::errc::unknown_field);
        if ((!index_value->holds<std::uint64_t>() &&
                !index_value->holds<std::int64_t>()) ||
            !score_value->is_number())
            return contract_error(cnetmod::json::errc::type_mismatch);

        std::uint64_t index{};
        if (index_value->holds<std::uint64_t>())
            index = index_value->get<std::uint64_t>();
        else
        {
            const auto signed_index = index_value->get<std::int64_t>();
            if (signed_index < 0)
                return contract_error(cnetmod::json::errc::type_mismatch);
            index = static_cast<std::uint64_t>(signed_index);
        }
        if (index > std::numeric_limits<std::size_t>::max())
            return contract_error(cnetmod::json::errc::type_mismatch);

        const auto score = score_value->as<double>();
        if (!std::isfinite(score) || score < 0.0 || score > 1.0)
            return contract_error(cnetmod::json::errc::type_mismatch);
        return scored_document{
            .index = static_cast<std::size_t>(index),
            .score = score};
    }
};

struct scores_response
{
    std::vector<scored_document> scores;

    [[nodiscard]] static auto from_document(
        const cnetmod::json::document& source)
        -> std::expected<scores_response, std::error_code>
    {
        if (!source.is_object())
            return contract_error(cnetmod::json::errc::type_mismatch);
        const auto* values = cnetmod::json::find(source, "scores");
        if (values == nullptr)
            return contract_error(cnetmod::json::errc::missing_field);
        if (source.size() != 1U)
            return contract_error(cnetmod::json::errc::unknown_field);
        if (!values->is_array())
            return contract_error(cnetmod::json::errc::type_mismatch);

        scores_response result;
        result.scores.reserve(values->size());
        for (const auto& value : values->get_array())
        {
            auto score = scored_document::from_document(value);
            if (!score)
                return std::unexpected(score.error());
            result.scores.push_back(std::move(*score));
        }
        return result;
    }
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
    auto document = cnetmod::json::parse_document(text);
    if (!document)
        return std::unexpected(document.error());
    auto response = queries_response::from_document(*document);
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
    auto response = routes_response::from_document(*document);
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
    auto response = scores_response::from_document(*document);
    if (!response)
        return std::unexpected(response.error());
    std::vector<std::pair<std::size_t, double>> result;
    result.reserve(response->scores.size());
    for (const auto& item : response->scores)
        result.emplace_back(item.index, item.score);
    return result;
}

} // namespace cnetmod::openai::detail
