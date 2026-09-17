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
import cnetmod.instrumentation.tracing;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.error;

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

struct default_database_result_adapter
{
    template <class Result>
    static auto adapt(Result&& result) -> query_result
    {
        return database_result_adapter<std::remove_cvref_t<Result>>::adapt(
            std::forward<Result>(result));
    }
};

/// Controls potentially sensitive SQL telemetry. Query text is excluded by
/// default because it can contain credentials or personal data.
struct sql_observation_options
{
    bool capture_query_text = false;
    std::size_t max_query_bytes = 2048;
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

/**
 * @brief Carries mapped models and native database diagnostics.
 *
 * The native error number and SQLSTATE remain available so callers can use
 * precise vendor diagnostics without parsing human-readable messages.
 */
template <class T> struct model_result
{
    std::vector<T> data;
    std::uint64_t affected_rows{};
    std::uint64_t last_insert_id{};
    std::string error_msg;
    std::string sql_state;
    std::uint32_t error_code{};

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
template <asynchronous_database_client Client,
    class ResultAdapter = default_database_result_adapter>
class database_session
{
public:
    explicit database_session(Client& client,
        sql_dialect dialect = sql_dialect::mysql) noexcept
        : client_(&client), dialect_(dialect), dialect_config_(get_dialect_config(dialect)) {}

    /**
     * @brief Creates a session pinned to one routed physical table.
     *
     * Model metadata remains unchanged. Every typed CRUD statement generated
     * by this session uses the supplied physical table, while raw SQL methods
     * keep their original behavior.
     */
    database_session(Client& client, std::string physical_table,
        sql_dialect dialect = sql_dialect::mysql)
        : client_(&client), dialect_(dialect), dialect_config_(get_dialect_config(dialect)), physical_table_(std::move(physical_table)) {}

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

    // Long-lived units of work need an explicit lifecycle because their
    // repositories may suspend between individual commands. Keep the dialect
    // specific statements inside the ORM boundary rather than duplicating
    // them in every primary-store adapter.
    auto begin_transaction(std::optional<isolation_level> isolation = std::nullopt)
        -> task<std::expected<void, std::string>>
    {
        auto started = co_await begin_expected_transaction(isolation);
        if (started.is_err())
            co_return std::unexpected(std::move(started.error_msg));
        co_return std::expected<void, std::string>{};
    }

    auto commit_transaction() -> task<std::expected<void, std::string>>
    {
        auto committed = co_await execute("COMMIT");
        if (committed.is_err())
            co_return std::unexpected(transaction_phase_error("commit", committed));
        co_return std::expected<void, std::string>{};
    }

    auto rollback_transaction() -> task<std::expected<void, std::string>>
    {
        auto rolled_back = co_await execute("ROLLBACK");
        if (rolled_back.is_err())
            co_return std::unexpected(transaction_phase_error("rollback", rolled_back));
        co_return std::expected<void, std::string>{};
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
        sql += quote_identifier(table_name<T>(), dialect_config_);
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
        sql += quote_identifier(table_name<T>(), dialect_config_);
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
        auto [sql, parameters] = query.build_select_sql(table_name<T>(), dialect_config_);
        co_return map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
    }

    template <Model T>
    auto count(const query_wrapper<T>& query)
        -> task<std::expected<std::size_t, std::string>>
    {
        auto [sql, parameters] = query.build_count_sql(table_name<T>(), dialect_config_);
        auto result = co_await execute_bound(std::move(sql), std::move(parameters));
        if (result.is_err())
            co_return std::unexpected(std::move(result.error_msg));
        if (result.rows.empty() || result.rows.front().empty())
            co_return std::size_t{};

        const auto& value = result.rows.front().front();
        if (value.is_uint64())
            co_return static_cast<std::size_t>(value.get_uint64());
        if (value.is_int64())
            co_return static_cast<std::size_t>(value.get_int64());
        co_return std::unexpected("count query returned a non-integral value");
    }

    template <Model T> auto remove(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        auto [sql, parameters] = query.build_delete_sql(table_name<T>(), dialect_config_);
        if (dialect_config_.supports_returning)
            sql += " RETURNING *";
        co_return map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
    }

    template <Model T> auto update(const update_wrapper<T>& update) -> task<model_result<T>>
    {
        auto [sql, parameters] = update.build_sql(table_name<T>(), dialect_config_);
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

    /**
     * @brief Observes a query without coupling the database to HTTP or OTEL.
     * An empty sink returns the original task without an observation frame.
     */
    auto query(std::string_view sql, const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end,
        sql_observation_options options = {}) -> task<query_result>
    {
        if (!on_end)
            return query(sql);
        return observe_sql(sql, parent, on_end, options, false);
    }

    /**
     * @brief Observes execution while preserving results and exceptions.
     */
    auto execute(std::string_view sql, const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end,
        sql_observation_options options = {}) -> task<query_result>
    {
        if (!on_end)
            return execute(sql);
        return observe_sql(sql, parent, on_end, options, true);
    }

    /**
     * @brief Observes bound execution without capturing parameter values.
     * The statement retains ownership of its bindings across suspension.
     */
    auto execute(parameterized_query statement, const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end,
        sql_observation_options options = {}) -> task<query_result>
    {
        if (!on_end)
            return execute(std::move(statement));
        return observe_sql(std::move(statement), parent, on_end, options, true);
    }

    /// Run an operation in a transaction without using exceptions as the
    /// expected failure path. The operation's value is returned after a
    /// successful commit; any failure attempts a rollback first.
    template <typename T, typename Function>
    requires std::invocable<Function&> && requires(Function& function) {
        { function() } -> std::same_as<task<std::expected<T, std::string>>>;
    }
    [[nodiscard]] auto transaction(Function function)
        -> task<std::expected<T, std::string>>
    {
        co_return co_await expected_transaction<T>(
            std::move(function), std::nullopt);
    }

    /// Expected-returning transaction with an explicit isolation level.
    template <typename T, typename Function>
    requires std::invocable<Function&> && requires(Function& function) {
        { function() } -> std::same_as<task<std::expected<T, std::string>>>;
    }
    [[nodiscard]] auto transaction(Function function, isolation_level isolation)
        -> task<std::expected<T, std::string>>
    {
        co_return co_await expected_transaction<T>(
            std::move(function), isolation);
    }

    template <typename Function>
    requires std::invocable<Function> && requires(Function function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function&& function) -> task<query_result>
    {
        auto started = co_await execute(
            dialect_ == sql_dialect::postgresql ? "BEGIN" : "START TRANSACTION");
        if (started.is_err())
            co_return started;

        std::string transaction_error;
        try
        {
            co_await function();
        }
        catch (const std::exception& error)
        {
            transaction_error = error.what();
        }
        catch (...)
        {
            transaction_error = "transaction failed";
        }

        if (transaction_error.empty())
            co_return co_await execute("COMMIT");

        (void)co_await execute("ROLLBACK");
        query_result result;
        result.error_msg = std::move(transaction_error);
        co_return result;
    }

    template <typename Function>
    requires std::invocable<Function> && requires(Function function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function&& function, isolation_level isolation)
        -> task<query_result>
    {
        const auto isolation_name = isolation_level_name(isolation);
        query_result started;
        if (dialect_ == sql_dialect::postgresql)
        {
            const auto begin = std::format(
                "BEGIN ISOLATION LEVEL {}", isolation_name);
            started = co_await execute(begin);
        }
        else
        {
            const auto configure = std::format(
                "SET TRANSACTION ISOLATION LEVEL {}", isolation_name);
            auto configured = co_await execute(configure);
            if (configured.is_err())
                co_return configured;
            started = co_await execute("START TRANSACTION");
        }
        if (started.is_err())
            co_return started;

        std::string transaction_error;
        try
        {
            co_await function();
        }
        catch (const std::exception& error)
        {
            transaction_error = error.what();
        }
        catch (...)
        {
            transaction_error = "transaction failed";
        }

        if (transaction_error.empty())
            co_return co_await execute("COMMIT");

        (void)co_await execute("ROLLBACK");
        query_result result;
        result.error_msg = std::move(transaction_error);
        co_return result;
    }

private:
    template <Model T>
    [[nodiscard]] auto table_name() const noexcept -> std::string_view
    {
        if (!physical_table_.empty())
            return physical_table_;
        return model_traits<T>::meta().table_name;
    }

    /**
     * @brief Owns observation inputs without starting an unexecuted operation.
     */
    struct pending_observation
    {
        instrumentation::trace_context parent;
        instrumentation::span_exporter sink;
        sql_observation_options options;
    };

    /**
     * @brief Captures sink ownership before returning the lazy database task.
     */
    template <typename Statement>
    auto observe_sql(Statement sql, const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& sink, sql_observation_options options,
        bool execution) -> task<query_result>
    {
        std::optional<pending_observation> observation;
        try
        {
            observation.emplace(pending_observation{parent, sink, options});
            auto pending = execute_observed(sql, observation, execution);
            // Transfer inputs only after the observation frame exists. The
            // first suspension below precedes all instrumentation and I/O.
            pending.handle().resume();
            return pending;
        }
        catch (...)
        {
            // Input-copy or frame-allocation failure leaves the SQL untouched.
            if constexpr (std::same_as<Statement, parameterized_query>)
                return execute(std::move(sql));
            else
                return execution ? execute(sql) : query(sql);
        }
    }

    /**
     * @brief Starts and completes observation within the executing coroutine.
     * The caller primes ownership transfer before returning this task. Source
     * references are never accessed after the explicit ownership suspension.
     */
    template <typename Statement>
    auto execute_observed(Statement& source, std::optional<pending_observation>& pending,
        bool execution) -> task<query_result>
    {
        static_assert(std::is_nothrow_move_constructible_v<Statement>);
        static_assert(std::is_nothrow_move_constructible_v<pending_observation>);
        auto sql = std::move(source);
        auto observation = std::move(pending);
        co_await std::suspend_always{};
        instrumentation::operation_scope scope;
        if (observation)
        {
            scope = instrumentation::operation_scope::start(observation->sink, [&]
                {
                    return instrumentation::start_client_span(observation->parent,
                        execution ? "SQL EXECUTE" : "SQL QUERY");
                });
            scope.annotate([&]
                {
                    const auto& options = observation->options;
                    std::vector<std::pair<std::string, std::string>> attributes{
                        {"db.system.name", dialect_ == sql_dialect::postgresql ? "postgresql" : "mysql"},
                        {"db.operation.name", execution ? "execute" : "query"}};
                    if (options.capture_query_text && options.max_query_bytes > 0)
                    {
                        if constexpr (std::same_as<Statement, parameterized_query>)
                            attributes.emplace_back("db.query.text", sql.query.substr(0, options.max_query_bytes));
                        else
                            attributes.emplace_back("db.query.text", sql.substr(0, options.max_query_bytes));
                    }
                    return attributes;
                });
            observation.reset();
        }
        try
        {
            query_result result;
            if constexpr (std::same_as<Statement, parameterized_query>)
                result = adapt(co_await client_->execute(std::move(sql)));
            else
                result = execution ? adapt(co_await client_->execute(sql))
                                   : adapt(co_await client_->query(sql));
            if (result.is_err())
                scope.annotate([&]
                    {
                        std::vector<std::pair<std::string, std::string>> attributes;
                        std::string code;
                        if (dialect_ == sql_dialect::mysql && result.error_code != 0)
                            code = std::to_string(result.error_code);
                        else if (dialect_ == sql_dialect::postgresql && result.sql_state.size() == 5 &&
                            std::ranges::all_of(result.sql_state, [](char character)
                                {
                                    return (character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z');
                                }))
                            code = result.sql_state;
                        if (!code.empty())
                        {
                            attributes.emplace_back("db.response.status_code", code);
                            attributes.emplace_back("error.type", std::move(code));
                        }
                        return attributes;
                    });
            scope.complete({result.is_err() ? instrumentation::operation_status::error
                                            : instrumentation::operation_status::success,
                {}});
            co_return result;
        }
        catch (const std::system_error& error)
        {
            scope.complete(instrumentation::classify_error(error.code()));
            throw;
        }
        catch (...)
        {
            scope.complete({instrumentation::operation_status::error, {}});
            throw;
        }
    }

    template <typename T, typename Function>
    auto expected_transaction(Function function,
        std::optional<isolation_level> isolation)
        -> task<std::expected<T, std::string>>
    {
        auto started = co_await begin_expected_transaction(isolation);
        if (started.is_err())
            co_return std::unexpected(std::move(started.error_msg));

        std::optional<std::expected<T, std::string>> operation;
        std::string exception_error;
        try
        {
            operation.emplace(co_await std::invoke(function));
        }
        catch (const std::exception& error)
        {
            exception_error = std::format(
                "transaction operation threw: {}", error.what());
        }
        catch (...)
        {
            exception_error = "transaction operation threw an unknown exception";
        }

        if (!exception_error.empty())
            co_return std::unexpected(co_await rollback_after_failure(
                std::move(exception_error)));

        if (!operation)
            co_return std::unexpected(co_await rollback_after_failure(
                "transaction operation completed without a result"));

        if (!*operation)
        {
            auto operation_error = operation->error().empty()
                ? std::string{"transaction operation failed"}
                : std::move(operation->error());
            co_return std::unexpected(co_await rollback_after_failure(
                std::move(operation_error)));
        }

        auto committed = co_await execute("COMMIT");
        if (committed.is_err())
        {
            auto commit_error = transaction_phase_error("commit", committed);
            co_return std::unexpected(co_await rollback_after_failure(
                std::move(commit_error)));
        }

        if constexpr (std::is_void_v<T>)
            co_return std::expected<void, std::string>{};
        else
            co_return std::move(**operation);
    }

    auto begin_expected_transaction(std::optional<isolation_level> isolation)
        -> task<query_result>
    {
        if (!isolation)
        {
            auto started = co_await execute(
                dialect_ == sql_dialect::postgresql ? "BEGIN" : "START TRANSACTION");
            if (started.is_err())
                started.error_msg = transaction_phase_error("begin", started);
            co_return started;
        }

        const auto isolation_name = isolation_level_name(*isolation);
        if (dialect_ == sql_dialect::postgresql)
        {
            auto started = co_await execute(std::format(
                "BEGIN ISOLATION LEVEL {}", isolation_name));
            if (started.is_err())
                started.error_msg = transaction_phase_error("begin", started);
            co_return started;
        }

        auto configured = co_await execute(std::format(
            "SET TRANSACTION ISOLATION LEVEL {}", isolation_name));
        if (configured.is_err())
        {
            configured.error_msg = transaction_phase_error(
                "configure transaction isolation", configured);
            co_return configured;
        }

        auto started = co_await execute("START TRANSACTION");
        if (started.is_err())
            started.error_msg = transaction_phase_error("begin", started);
        co_return started;
    }

    auto rollback_after_failure(std::string primary_error)
        -> task<std::string>
    {
        auto rolled_back = co_await execute("ROLLBACK");
        if (rolled_back.is_err())
        {
            primary_error += "; ";
            primary_error += transaction_phase_error("rollback", rolled_back);
        }
        co_return primary_error;
    }

    static auto transaction_phase_error(std::string_view phase,
        const query_result& result) -> std::string
    {
        if (result.error_msg.empty())
            return std::format("transaction {} failed", phase);
        return std::format("transaction {} failed: {}", phase,
            result.error_msg);
    }

    static auto isolation_level_name(isolation_level isolation)
        -> std::string_view
    {
        switch (isolation)
        {
        case isolation_level::read_uncommitted:
            return "READ UNCOMMITTED";
        case isolation_level::repeatable_read:
            return "REPEATABLE READ";
        case isolation_level::serializable:
            return "SERIALIZABLE";
        case isolation_level::read_committed:
        default:
            return "READ COMMITTED";
        }
    }

    template <class Result> static auto adapt(Result&& result) -> query_result
    {
        return ResultAdapter::adapt(std::forward<Result>(result));
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
        mapped.error_code = result.error_code;
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
    std::string physical_table_;
};

} // namespace cnetmod::orm
