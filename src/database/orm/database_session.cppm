/**
 * @brief Protocol-neutral database execution and transaction context.
 */
export module cnetmod.orm.database_session;

export import cnetmod.orm.database_backend;

import std;
import cnetmod.orm.mapper_operations;
import cnetmod.coro.task;
import cnetmod.orm.interceptor_chain;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_query_data;
import cnetmod.instrumentation.tracing;

export namespace cnetmod::orm {

/**
 * @brief Owns one database execution context without exposing model CRUD.
 *
 * Gateways construct this facade for a leased protocol client. It exposes raw
 * parameterized execution, transaction control, diagnostics instrumentation
 * and dialect selection. Model metadata, SQL generation and typed result
 * mapping remain implementation details of `mapper<T>`.
 */
template <asynchronous_database_client Client,
    class ResultAdapter = default_database_result_adapter>
class database_session final
    : private detail::mapper_operations<Client, ResultAdapter>
{
    using operations_type = detail::mapper_operations<Client, ResultAdapter>;
    friend struct detail::mapper_session_access;

public:
    using client_type = Client;
    using result_adapter_type = ResultAdapter;

    explicit database_session(Client& client,
        sql_dialect dialect = sql_dialect::mysql,
        std::shared_ptr<const interceptor_chain> interceptors = {}) noexcept
        : operations_type(client, dialect, std::move(interceptors))
    {
    }

    database_session(Client& client, std::string physical_table,
        sql_dialect dialect = sql_dialect::mysql,
        std::shared_ptr<const interceptor_chain> interceptors = {})
        : operations_type(client, std::move(physical_table), dialect,
              std::move(interceptors))
    {
    }

    using operations_type::begin_transaction;
    using operations_type::commit_transaction;
    using operations_type::dialect;
    using operations_type::rollback_transaction;
    using operations_type::transaction;
    using operations_type::underlying;

    auto query(std::string_view sql) -> task<query_result>
    {
        return operations_type::query(sql);
    }

    auto execute(std::string_view sql) -> task<query_result>
    {
        return operations_type::execute(sql);
    }

    auto execute(parameterized_query statement) -> task<query_result>
    {
        return operations_type::execute(std::move(statement));
    }

    auto query(std::string_view sql,
        const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end,
        sql_observation_options options = {}) -> task<query_result>
    {
        return operations_type::query(sql, parent, on_end, options);
    }

    auto execute(std::string_view sql,
        const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end,
        sql_observation_options options = {}) -> task<query_result>
    {
        return operations_type::execute(sql, parent, on_end, options);
    }

    auto execute(parameterized_query statement,
        const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end,
        sql_observation_options options = {}) -> task<query_result>
    {
        return operations_type::execute(
            std::move(statement), parent, on_end, options);
    }

private:
    /**
     * @brief Grants the Mapper bridge access to its private implementation.
     */
    [[nodiscard]] auto mapper_operations() noexcept -> operations_type&
    {
        return *this;
    }
};

} // namespace cnetmod::orm
