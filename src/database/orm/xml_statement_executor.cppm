/**
 * @brief Provider-neutral execution of MyBatis-style XML statements.
 */
export module cnetmod.orm.xml_statement_executor;

import std;
import cnetmod.coro.task;
import cnetmod.orm.dynamic_sql;
import cnetmod.orm.model_reflection;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.xml_mapper_registry;

export namespace cnetmod::orm {

/**
 * @brief Executes XML statements without imposing a model projection.
 *
 * The returned query_result is the normalized database result, not a native
 * MySQL or PostgreSQL protocol object. Typed XML mappers delegate to this
 * executor before applying CNETMOD_MODEL or resultMap projection.
 */
template <typename Session>
class xml_statement_executor
{
public:
    xml_statement_executor(Session& session,
        const mapper_registry& registry) noexcept
        : session_(&session), registry_(&registry)
    {
    }

    /**
     * @brief Executes an XML select and preserves columns, rows and diagnostics.
     */
    auto select(std::string_view statement_id,
        const param_context& parameters) -> task<query_result>
    {
        co_return co_await execute_statement(statement_id, parameters);
    }

    /**
     * @brief Executes an XML write and preserves affected-row diagnostics.
     */
    auto execute(std::string_view statement_id,
        const param_context& parameters) -> task<query_result>
    {
        co_return co_await execute_statement(statement_id, parameters);
    }

private:
    auto execute_statement(std::string_view statement_id,
        const param_context& parameters) -> task<query_result>
    {
        auto statement = build(statement_id, parameters);
        if (!statement)
        {
            query_result failure;
            failure.error_msg = std::move(statement.error());
            co_return failure;
        }
        co_return co_await session_->execute(std::move(*statement));
    }

    auto build(std::string_view statement_id,
        const param_context& parameters) const
        -> std::expected<parameterized_query, std::string>
    {
        const auto* statement = registry_->find_statement(statement_id);
        if (!statement)
            return std::unexpected(
                "XML statement not found: " + std::string{statement_id});

        const auto name_space = registry_->get_namespace(statement_id);
        static const fragment_map empty_fragments;
        const auto* fragments = registry_->get_fragments(name_space);
        try
        {
            dynamic_sql_processor processor{format_options{}};
            auto built = processor.process(*statement, parameters,
                fragments ? *fragments : empty_fragments);
            if (session_->dialect() == sql_dialect::postgresql)
            {
                std::string normalized;
                normalized.reserve(built.sql.size() + built.params.size() * 2);
                std::size_t parameter = 1;
                for (std::size_t index = 0; index < built.sql.size(); ++index)
                {
                    if (built.sql[index] == '{' &&
                        index + 1 < built.sql.size() &&
                        built.sql[index + 1] == '}')
                    {
                        normalized += std::format("${}", parameter++);
                        ++index;
                    }
                    else
                    {
                        normalized.push_back(built.sql[index]);
                    }
                }
                built.sql = std::move(normalized);
            }
            return parameterized_query{
                std::move(built.sql), std::move(built.params)};
        }
        catch (const std::exception& error)
        {
            return std::unexpected(error.what());
        }
    }

    Session* session_;
    const mapper_registry* registry_;
};

} // namespace cnetmod::orm
