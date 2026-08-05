module cnetmod.protocol.mysql;

import std;
import :types;
import :connection_client;
import :format_sql;
import :orm_dynamic_sql;
import :orm_xml_crud;
import :orm_xml_mapper;
import cnetmod.coro.task;
import cnetmod.core.log;
import cnetmod.utils.flat_map;

namespace cnetmod::orm::mysql_detail {
using namespace cnetmod::mysql;
using namespace cnetmod::orm;

const fragment_map mapper_session::empty_fragments_{};

mapper_session::mapper_session(client& cli, mapper_registry& registry) noexcept
    : cli_(cli), registry_(registry) {}

void mapper_session::set_sql_logging(bool enabled) noexcept
{
    log_sql_ = enabled;
}

auto mapper_session::last_generated_sql() const noexcept -> std::string_view
{
    return last_sql_;
}

auto mapper_session::last_final_sql() const noexcept -> std::string_view
{
    return last_final_sql_;
}

auto mapper_session::execute(std::string_view statement_id,
    const param_context& ctx) -> task<exec_result>
{
    auto sql_result = build_sql(statement_id, ctx);
    if (!sql_result)
    {
        exec_result result;
        result.error_msg = sql_result.error();
        co_return result;
    }

    auto& [sql, params] = *sql_result;
    last_sql_ = sql;

    auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
    if (!final_sql_result)
    {
        exec_result result;
        result.error_msg = "SQL formatting error";
        co_return result;
    }

    last_final_sql_ = *final_sql_result;

    if (log_sql_ && final_sql_result->size() < 500)
    {
        logger::detail::write_log_no_src(logger::level::debug,
            std::format("[SQL] Generated: {}", sql));
        logger::detail::write_log_no_src(
            logger::level::debug,
            std::format("[SQL] Final: {}", *final_sql_result));
    }

    auto rs = co_await cli_.execute(*final_sql_result);

    exec_result result;
    result.affected_rows = rs.affected_rows;
    result.last_insert_id = rs.last_insert_id;
    result.error_msg = rs.error_msg;
    co_return result;
}

auto mapper_session::execute(
    std::string_view statement_id,
    cnetmod::flat_map<std::string, cnetmod::orm::param_value> params)
    -> task<exec_result>
{
    co_return co_await execute(statement_id,
        param_context::from_map(std::move(params)));
}

auto mapper_session::execute_query(std::string_view statement_id,
    const param_context& ctx)
    -> task<result_set>
{
    auto sql_result = build_sql(statement_id, ctx);
    if (!sql_result)
    {
        result_set result;
        result.error_msg = sql_result.error();
        co_return result;
    }

    auto& [sql, params] = *sql_result;
    last_sql_ = sql;

    auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
    if (!final_sql_result)
    {
        result_set result;
        result.error_msg = "SQL formatting error";
        co_return result;
    }

    last_final_sql_ = *final_sql_result;

    if (log_sql_ && final_sql_result->size() < 500)
    {
        logger::detail::write_log_no_src(logger::level::debug,
            std::format("[SQL] Generated: {}", sql));
        logger::detail::write_log_no_src(
            logger::level::debug,
            std::format("[SQL] Final: {}", *final_sql_result));
    }

    auto mysql_result = co_await cli_.execute(*final_sql_result);
    co_return mysql_adapt_result(mysql_result);
}

auto mapper_session::execute_query(
    std::string_view statement_id,
    cnetmod::flat_map<std::string, cnetmod::orm::param_value> params)
    -> task<result_set>
{
    co_return co_await execute_query(statement_id,
        param_context::from_map(std::move(params)));
}

auto mapper_session::query_object_graph(std::string_view statement_id,
    const param_context& ctx)
    -> task<std::expected<std::vector<mapped_object>, std::string>>
{
    const auto* statement = registry_.find_statement(statement_id);
    if (!statement)
        co_return std::unexpected("statement not found: " + std::string(statement_id));
    const auto result_map_id = statement->attr("resultMap");
    if (result_map_id.empty())
        co_return std::unexpected("select statement requires a resultMap attribute");

    const auto namespace_id = registry_.get_namespace(statement_id);
    const auto* maps = registry_.result_maps(namespace_id);
    const auto* root_map = maps ? maps->find(result_map_id)
                                : registry_.find_result_map(result_map_id);
    if (!root_map || !maps)
        co_return std::unexpected("resultMap not found: " + std::string(result_map_id));

    auto rows = co_await execute_query(statement_id, ctx);
    if (rows.is_err())
        co_return std::unexpected(rows.error_msg);
    auto graph = result_map_applier::materialize_joined(*root_map, rows, *maps);

    // `column` becomes the parameter name for the referenced nested select.
    for (auto& parent : graph)
    {
        auto load_relation = [&](std::string_view property, std::string_view column,
                                 std::string_view select, std::string_view nested_map_id,
                                 bool many) -> task<std::expected<void, std::string>> {
            if (select.empty())
                co_return {};
            const auto* source = root_map->find_by_column(column);
            const auto value_it = parent.values.find(
                source ? source->property : std::string(column));
            if (value_it == parent.values.end())
                co_return {};
            const auto* nested_statement = registry_.find_statement(select);
            if (!nested_statement)
                co_return std::unexpected("nested statement not found: " + std::string(select));
            const auto nested_namespace = registry_.get_namespace(select);
            const auto* nested_maps = registry_.result_maps(nested_namespace);
            const auto resolved_map_id = nested_map_id.empty()
                ? nested_statement->attr("resultMap") : nested_map_id;
            const auto* nested_map = nested_maps ? nested_maps->find(resolved_map_id)
                : registry_.find_result_map(resolved_map_id);
            if (!nested_map || !nested_maps)
                co_return std::unexpected("nested resultMap not found: " + std::string(resolved_map_id));

            auto nested = co_await execute_query(select,
                param_context::from_map({{std::string(column), value_it->second}}));
            if (nested.is_err())
                co_return std::unexpected(nested.error_msg);
            auto objects = result_map_applier::materialize_joined(*nested_map, nested, *nested_maps);
            if (many)
                parent.collections[std::string(property)] = std::move(objects);
            else if (!objects.empty())
                parent.associations[std::string(property)] = std::move(objects.front());
            co_return {};
        };

        for (const auto& relation : root_map->associations)
            if (auto loaded = co_await load_relation(relation.property, relation.column,
                    relation.select, relation.result_map, false); !loaded)
                co_return std::unexpected(loaded.error());
        for (const auto& relation : root_map->collections)
            if (auto loaded = co_await load_relation(relation.property, relation.column,
                    relation.select, relation.result_map, true); !loaded)
                co_return std::unexpected(loaded.error());
    }
    co_return graph;
}

auto mapper_session::underlying() noexcept -> client&
{
    return cli_;
}

auto mapper_session::registry() noexcept -> mapper_registry&
{
    return registry_;
}

auto mapper_session::build_sql(std::string_view statement_id,
    const param_context& ctx)
    -> std::expected<built_dynamic_sql, std::string>
{
    auto* stmt = registry_.find_statement(statement_id);
    if (!stmt)
        return std::unexpected("statement not found: " + std::string(statement_id));

    auto ns = registry_.get_namespace(statement_id);
    auto* fragments = registry_.get_fragments(ns);
    if (!fragments)
        fragments = &empty_fragments_;

    const cnetmod::orm::format_options orm_options{
        .backslash_escapes = cli_.current_format_opts().backslash_escapes};
    dynamic_sql_processor processor(orm_options);
    return processor.process(*stmt, ctx, *fragments);
}

} // namespace cnetmod::orm::mysql_detail
