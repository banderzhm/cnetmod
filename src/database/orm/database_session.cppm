export module cnetmod.orm.database_session;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_statement_formatting;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.model_metadata;
import cnetmod.orm.result_mapper;
import cnetmod.orm.query_wrapper;
import cnetmod.coro.task;
import cnetmod.protocol.http.middleware.tracing;

export namespace cnetmod::orm {

/// Protocol result adaptation point. Protocol modules with their own wire
/// result representation specialize this at their boundary; database_session
/// itself never needs to know those protocol types.
template <class Result> struct database_result_adapter
{
    static auto adapt(Result&& result) -> query_result
    {
        static_assert(std::same_as<std::remove_cvref_t<Result>, query_result>,
            "database client result needs a database_result_adapter specialization");
        return std::forward<Result>(result);
    }
};

template <class Client>
concept asynchronous_database_client = requires(Client& client,
    std::string_view sql,
    parameterized_query statement,
    isolation_level isolation) {
    client.query(sql);
    client.execute(sql);
    client.execute(std::move(statement));
};

/// Typed result shared by every database dialect.  It deliberately preserves
/// the raw operation metadata while making the row-to-model mapping explicit.
template <class T> struct model_result
{
    std::vector<T> data;
    std::uint64_t affected_rows{};
    std::uint64_t last_insert_id{};
    std::string error_msg;
    std::string sql_state;

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return error_msg.empty();
    }

    [[nodiscard]] auto is_err() const noexcept -> bool
    {
        return !ok();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return data.empty();
    }

    [[nodiscard]] auto first() const -> std::optional<T>
    {
        return data.empty() ? std::nullopt : std::optional<T>{data.front()};
    }
};

/// Protocol-independent session used by ORM repositories and generated mappers.
/// Higher-level model modules depend on this contract, never a wire protocol.
///
/// Raw SQL, typed CRUD and row-to-model mapping share this one session.  The
/// selected dialect owns only identifier quoting, placeholder spelling and
/// RETURNING support; protocol clients continue to own the wire operation.
template <asynchronous_database_client Client>
class database_session
{
public:
    explicit database_session(Client& client,
        sql_dialect dialect = sql_dialect::mysql) noexcept
        : client_(&client), dialect_(dialect), dialect_config_(get_dialect_config(dialect)) {}

    [[nodiscard]] auto underlying() noexcept -> Client&
    {
        return *client_;
    }

    auto query(std::string_view sql) -> task<query_result>
    {
        co_return adapt(co_await client_->query(sql));
    }

    auto execute(std::string_view sql) -> task<query_result>
    {
        co_return adapt(co_await client_->execute(sql));
    }

    auto execute(parameterized_query query) -> task<query_result>
    {
        // Every protocol adapter accepts the ORM's common parameter object.
        // PostgreSQL sends it as a bound extended query; MySQL renders it with
        // its connection charset/escaping rules before COM_QUERY.  Keeping
        // that conversion at the protocol edge avoids both SQL interpolation
        // in repositories and dialect-specific client knowledge here.
        co_return adapt(co_await client_->execute(std::move(query)));
    }

    [[nodiscard]] auto dialect() const noexcept -> sql_dialect
    {
        return dialect_;
    }

    // ---------------------------------------------------------------------
    // Unified model CRUD
    // ---------------------------------------------------------------------

    template <Model T> auto find_all() -> task<model_result<T>>
    {
        co_return co_await find(query_wrapper<T>{});
    }

    template <Model T> auto find_by_id(param_value id) -> task<model_result<T>>
    {
        const auto& meta = model_traits<T>::meta();
        const auto* primary_key = meta.pk();
        if (!primary_key)
            co_return failure<T>("model has no primary key");

        query_wrapper<T> query;
        query.eq(primary_key->col.column_name, id).limit(1);
        co_return co_await find(query);
    }

    template <Model T>
    auto find_one_by(std::string_view column, param_value value) -> task<model_result<T>>
    {
        const auto& meta = model_traits<T>::meta();
        const auto* field = meta.find_column(column);
        if (!field)
            co_return failure<T>(std::format("model '{}' has no mapped column '{}'",
                meta.table_name, column));

        query_wrapper<T> query;
        query.eq(field->col.column_name, value).limit(1);
        co_return co_await find(query);
    }

    template <Model T> auto insert(T& model) -> task<model_result<T>>
    {
        const auto& meta = model_traits<T>::meta();
        const auto fields = meta.insertable_fields();
        if (fields.empty())
            co_return failure<T>("model has no insertable fields");

        std::string sql = "INSERT INTO ";
        sql += quote_identifier(meta.table_name, dialect_config_);
        sql += " (";
        std::vector<param_value> parameters;
        parameters.reserve(fields.size());
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            if (index != 0)
                sql += ", ";
            sql += quote_identifier(fields[index]->col.column_name, dialect_config_);
            parameters.push_back(fields[index]->getter(model));
        }
        sql += ") VALUES (";
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            if (index != 0)
                sql += ", ";
            sql += make_placeholder(static_cast<int>(index + 1), dialect_config_);
        }
        sql += ")";
        if (dialect_config_.supports_returning)
            sql += " RETURNING *";

        auto result = map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
        if (result.is_err())
            co_return result;
        if (!result.data.empty())
        {
            model = result.data.front();
        }
        else
        {
            fill_insert_id<T>(model, result.last_insert_id);
            result.data.push_back(model);
        }
        co_return result;
    }

    template <Model T> auto update(const T& model) -> task<model_result<T>>
    {
        const auto& meta = model_traits<T>::meta();
        const auto* primary_key = meta.pk();
        if (!primary_key)
            co_return failure<T>("model has no primary key");

        const auto fields = meta.updatable_fields();
        if (fields.empty())
            co_return failure<T>("model has no updatable fields");

        std::string sql = "UPDATE ";
        sql += quote_identifier(meta.table_name, dialect_config_);
        sql += " SET ";
        std::vector<param_value> parameters;
        parameters.reserve(fields.size() + 1);
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            if (index != 0)
                sql += ", ";
            sql += quote_identifier(fields[index]->col.column_name, dialect_config_);
            sql += " = ";
            sql += make_placeholder(static_cast<int>(index + 1), dialect_config_);
            parameters.push_back(fields[index]->getter(model));
        }
        parameters.push_back(primary_key->getter(model));
        sql += " WHERE ";
        sql += quote_identifier(primary_key->col.column_name, dialect_config_);
        sql += " = ";
        sql += make_placeholder(static_cast<int>(parameters.size()), dialect_config_);
        if (dialect_config_.supports_returning)
            sql += " RETURNING *";
        co_return map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
    }

    template <Model T> auto remove(const T& model) -> task<model_result<T>>
    {
        const auto& meta = model_traits<T>::meta();
        const auto* primary_key = meta.pk();
        if (!primary_key)
            co_return failure<T>("model has no primary key");

        co_return co_await remove_by<T>(primary_key->col.column_name,
            primary_key->getter(model));
    }

    template <Model T>
    auto remove_by(std::string_view column, param_value value) -> task<model_result<T>>
    {
        const auto& meta = model_traits<T>::meta();
        const auto* field = meta.find_column(column);
        if (!field)
            co_return failure<T>(std::format("model '{}' has no mapped column '{}'",
                meta.table_name, column));

        query_wrapper<T> query;
        query.eq(field->col.column_name, value);
        co_return co_await remove(query);
    }

    template <Model T> auto remove_by_id(param_value id) -> task<model_result<T>>
    {
        const auto* primary_key = model_traits<T>::meta().pk();
        if (!primary_key)
            co_return failure<T>("model has no primary key");
        co_return co_await remove_by<T>(primary_key->col.column_name, std::move(id));
    }

    template <Model T> auto find(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        auto [sql, parameters] = query.build_select_sql(dialect_);
        co_return map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
    }

    template <Model T> auto remove(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        auto [sql, parameters] = query.build_delete_sql(dialect_);
        if (dialect_config_.supports_returning)
            sql += " RETURNING *";
        co_return map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
    }

    template <Model T> auto update(const update_wrapper<T>& update) -> task<model_result<T>>
    {
        auto [sql, parameters] = update.build_sql(dialect_);
        if (dialect_config_.supports_returning)
            sql += " RETURNING *";
        co_return map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
    }

    /// Execute a tagged wrapper directly. A query_wrapper defaults to SELECT;
    /// callers opt into DELETE explicitly with wrapper.as_delete().
    template <Model T> auto execute(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        if (query.execution_kind() == query_execution_kind::delete_)
            co_return co_await remove(query);
        co_return co_await find(query);
    }

    /// update_wrapper already represents exactly one SQL operation.
    template <Model T> auto execute(const update_wrapper<T>& update) -> task<model_result<T>>
    {
        co_return co_await this->update(update);
    }

    /// Database protocols do not carry W3C headers, so tracing is explicit at
    /// this boundary: it derives a child context and hands the completed
    /// client span to the same exporter used by the enclosing HTTP request.
    auto query(std::string_view sql, const http::tracing::trace_context& parent,
        http::tracing::span_exporter on_end) -> task<query_result>
    {
        auto span = http::tracing::start_client_span(parent, "SQL QUERY",
            {{"db.system", "sql"}, {"db.operation", "query"},
                {"db.statement", std::string(sql)}});
        auto result = adapt(co_await client_->query(sql));
        if (on_end)
        {
            try
            {
                on_end(http::tracing::finish_client_span(std::move(span), result.is_err()));
            }
            catch (...)
            {
                // Observability must not alter database semantics.
            }
        }
        co_return result;
    }

    auto execute(std::string_view sql, const http::tracing::trace_context& parent,
        http::tracing::span_exporter on_end) -> task<query_result>
    {
        auto span = http::tracing::start_client_span(parent, "SQL EXECUTE",
            {{"db.system", "sql"}, {"db.operation", "execute"},
                {"db.statement", std::string(sql)}});
        auto result = adapt(co_await client_->execute(sql));
        if (on_end)
        {
            try
            {
                on_end(http::tracing::finish_client_span(std::move(span), result.is_err()));
            }
            catch (...)
            {
                // Observability must not alter database semantics.
            }
        }
        co_return result;
    }

    template <typename Function>
    requires std::invocable<Function> && requires(Function function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function&& function) -> task<query_result>
    {
        co_return adapt(co_await client_->transaction(std::forward<Function>(function)));
    }

    template <typename Function>
    requires std::invocable<Function> && requires(Function function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function&& function, isolation_level isolation)
        -> task<query_result>
    {
        co_return adapt(co_await client_->transaction(std::forward<Function>(function), isolation));
    }

private:
    template <class Result> static auto adapt(Result&& result) -> query_result
    {
        return database_result_adapter<std::remove_cvref_t<Result>>::adapt(
            std::forward<Result>(result));
    }

    auto execute_bound(std::string sql, std::vector<param_value> parameters)
        -> task<query_result>
    {
        // `sql` remains in this coroutine frame until the protocol operation
        // consumes the string_view held by parameterized_query.
        co_return co_await execute(parameterized_query{sql, std::move(parameters)});
    }

    template <Model T> static auto failure(std::string message) -> model_result<T>
    {
        model_result<T> result;
        result.error_msg = std::move(message);
        return result;
    }

    template <Model T> static auto map(query_result result) -> model_result<T>
    {
        model_result<T> mapped;
        mapped.affected_rows = result.affected_rows;
        mapped.last_insert_id = result.last_insert_id;
        mapped.sql_state = std::move(result.sql_state);
        if (result.is_err())
        {
            mapped.error_msg = std::move(result.error_msg);
            return mapped;
        }
        mapped.data = from_result_set<T>(result);
        return mapped;
    }

    Client* client_;
    sql_dialect dialect_;
    dialect_config dialect_config_;
};

} // namespace cnetmod::orm
