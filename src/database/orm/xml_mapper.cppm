/**
 * @brief Typed execution of MyBatis-style XML statements over an ORM session.
 */
export module cnetmod.orm.xml_mapper;

import std;
import cnetmod.coro.task;
import cnetmod.orm.dynamic_sql;
import cnetmod.orm.model_metadata;
import cnetmod.orm.model_reflection;
import cnetmod.orm.repository_contract;
import cnetmod.orm.result_mapper;
import cnetmod.orm.result_map;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.xml_mapper_registry;
import cnetmod.orm.xml_statement_executor;

export namespace cnetmod::orm {

/**
 * @brief Materializes a provider-neutral XML query result as a typed record.
 *
 * Projection records declared with CNETMOD_PROJECTION and persistent models
 * declared with CNETMOD_MODEL share this mapping path. A statement resultMap
 * is honored when present; otherwise columns bind by their declared names.
 */
template <ResultRecord Record>
auto map_xml_select_result(const mapper_registry& registry,
    std::string_view statement_id, query_result source)
    -> model_result<Record>
{
    auto direct = [](query_result value) -> model_result<Record>
    {
        model_result<Record> result;
        result.affected_rows = value.affected_rows;
        result.last_insert_id = value.last_insert_id;
        result.error_msg = std::move(value.error_msg);
        result.sql_state = std::move(value.sql_state);
        result.error_code = value.error_code;
        if (result.error_msg.empty())
            result.data = from_result_set<Record>(value);
        return result;
    };

    const auto result_map_id = registry.statement_result_map(statement_id);
    if (result_map_id.empty() || source.is_err())
        return direct(std::move(source));

    const auto name_space = registry.get_namespace(statement_id);
    std::string qualified_result_map;
    if (result_map_id.contains('.'))
        qualified_result_map = result_map_id;
    else
        qualified_result_map = std::format(
            "{}.{}", name_space, result_map_id);
    const auto* definition = registry.find_result_map(qualified_result_map);
    const auto* definitions = registry.result_maps(name_space);
    if (!definition || !definitions)
    {
        model_result<Record> result;
        result.error_msg = "XML resultMap not found: " + qualified_result_map;
        result.framework_error = std::make_error_code(
            std::errc::invalid_argument);
        return result;
    }

    model_result<Record> result;
    result.affected_rows = source.affected_rows;
    result.last_insert_id = source.last_insert_id;
    result.sql_state = std::move(source.sql_state);
    result.error_code = source.error_code;
    result.data = from_mapped_objects<Record>(
        result_map_applier::materialize_joined(
            *definition, source, *definitions));
    return result;
}

/**
 * @brief Adds XML-defined statements to the same typed Mapper session.
 *
 * The class is protocol-neutral. It renders XML dynamic SQL into the common
 * parameterized query contract, then delegates execution, interception,
 * diagnostics and transaction ownership to the supplied database session.
 */
template <Model T, typename Session>
class xml_mapper
{
public:
    xml_mapper(Session& session, const mapper_registry& registry) noexcept
        : session_(&session), registry_(&registry)
    {
    }

    auto select(std::string_view statement_id, const param_context& parameters)
        -> task<model_result<T>>
    {
        return select_impl(session_, registry_, std::string{statement_id},
            parameters);
    }

    auto select(std::string_view statement_id, const T& parameters)
        -> task<model_result<T>>
    {
        return select_impl(session_, registry_, std::string{statement_id},
            param_context::from_model(parameters));
    }

    auto select_one(std::string_view statement_id,
        const param_context& parameters,
        single_result_policy policy = single_result_policy::require_unique)
        -> task<model_result<T>>
    {
        return select_one_impl(session_, registry_, std::string{statement_id},
            parameters, policy);
    }

    auto execute(std::string_view statement_id,
        const param_context& parameters) -> task<model_result<T>>
    {
        return execute_impl(session_, registry_, std::string{statement_id},
            parameters);
    }

    auto execute(std::string_view statement_id, const T& parameters)
        -> task<model_result<T>>
    {
        return execute_impl(session_, registry_, std::string{statement_id},
            param_context::from_model(parameters));
    }

private:
    static auto select_impl(Session* session, const mapper_registry* registry,
        std::string statement_id, param_context parameters)
        -> task<model_result<T>>
    {
        xml_statement_executor<Session> executor{*session, *registry};
        auto result = co_await executor.select(statement_id, parameters);
        co_return map_xml_select_result<T>(
            *registry, statement_id, std::move(result));
    }

    static auto select_one_impl(Session* session,
        const mapper_registry* registry, std::string statement_id,
        param_context parameters, single_result_policy policy)
        -> task<model_result<T>>
    {
        auto result = co_await select_impl(session, registry,
            std::move(statement_id), std::move(parameters));
        if (result.is_err() || result.data.size() <= 1)
            co_return result;
        if (policy == single_result_policy::first)
        {
            result.data.resize(1);
            co_return result;
        }
        result.data.clear();
        result.error_msg = "XML statement returned more than one row";
        result.framework_error = std::make_error_code(
            std::errc::result_out_of_range);
        co_return result;
    }

    static auto execute_impl(Session* session, const mapper_registry* registry,
        std::string statement_id, param_context parameters)
        -> task<model_result<T>>
    {
        xml_statement_executor<Session> executor{*session, *registry};
        auto result = co_await executor.execute(statement_id, parameters);
        co_return map(std::move(result), false);
    }

    static auto map(query_result source, bool include_rows = true)
        -> model_result<T>
    {
        model_result<T> result;
        result.affected_rows = source.affected_rows;
        result.last_insert_id = source.last_insert_id;
        result.error_msg = std::move(source.error_msg);
        result.sql_state = std::move(source.sql_state);
        result.error_code = source.error_code;
        if (include_rows && result.error_msg.empty())
            result.data = from_result_set<T>(source);
        return result;
    }

    Session* session_;
    const mapper_registry* registry_;
};

} // namespace cnetmod::orm
