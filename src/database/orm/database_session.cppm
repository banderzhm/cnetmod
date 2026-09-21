export module cnetmod.orm.database_session;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_statement_formatting;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.model_metadata;
import cnetmod.orm.result_mapper;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.interceptor_chain;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.automatic_field_fill;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
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

/**
 * @brief Selects the cardinality contract for a single-row query.
 *
 * `require_unique` rejects a query that matches more than one row. `first`
 * deliberately returns the first matching row and must therefore be chosen
 * explicitly by callers that do not require uniqueness.
 */
enum class single_result_policy
{
    require_unique,
    first
};

/**
 * @brief Explicit authorization for an unbounded DELETE operation.
 */
struct allow_full_table_t
{
    explicit constexpr allow_full_table_t() = default;
};

inline constexpr allow_full_table_t allow_full_table{};

using projection_row = std::map<std::string, field_value, std::less<>>;

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
    std::error_code framework_error;
    /**
     * @brief Batch location for failures produced by a batch operation.
     *
     * The fields are unset for non-batch operations. Keeping the location in
     * the common result prevents callers from parsing error text to identify
     * the failed item.
     */
    std::optional<std::size_t> batch_index;
    std::optional<std::size_t> item_index;
    std::string operation;

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return error_msg.empty() && !framework_error;
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

/**
 * @brief Carries a page of projected values and complete query diagnostics.
 */
template <class T> struct page_result
{
    model_result<T> records;
    std::size_t total{};
    std::size_t page = 1;
    std::size_t page_size = 20;
    std::size_t total_pages{};

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return records.ok();
    }

    [[nodiscard]] auto has_next() const noexcept -> bool
    {
        return page < total_pages;
    }

    [[nodiscard]] auto has_previous() const noexcept -> bool
    {
        return page > 1 && total_pages > 0;
    }
};

/**
 * @brief Bounds a cooperative, page-backed ORM row stream.
 */
struct stream_options
{
    std::size_t batch_size = 256;
    std::size_t max_rows = std::numeric_limits<std::size_t>::max();
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

/**
 * @brief Stateful, bounded cursor policy for database sessions.
 *
 * The generic session backend implements this contract with bounded pages;
 * protocol clients that expose a wire cursor may provide a more direct
 * implementation through their own adapter without changing repository code.
 */
struct cursor_options
{
    std::size_t batch_size = 256;
    std::size_t max_rows = std::numeric_limits<std::size_t>::max();
    std::optional<std::chrono::steady_clock::time_point> deadline;
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
    template <Model T>
    class cursor
    {
    public:
        cursor(database_session& owner, query_wrapper<T> query,
            cursor_options options)
            : owner_(&owner), query_(std::move(query)), options_(options)
        {
        }

        /**
         * @brief Fetches the next bounded batch, returning an empty batch at EOF.
         */
        auto next() -> task<model_result<T>>
        {
            model_result<T> result;
            if (!owner_ || exhausted_ || offset_ >= options_.max_rows)
            {
                exhausted_ = true;
                co_return result;
            }
            if (options_.deadline && std::chrono::steady_clock::now() >=
                    *options_.deadline)
            {
                result.error_msg = "cursor deadline exceeded";
                result.framework_error = std::make_error_code(
                    std::errc::timed_out);
                co_return result;
            }
            auto bounded = query_;
            bounded.limit(static_cast<std::int64_t>(std::min(
                options_.batch_size, options_.max_rows - offset_)))
                .offset(static_cast<std::int64_t>(offset_));
            result = co_await owner_->template find<T>(bounded);
            if (result.is_err())
                co_return result;
            offset_ += result.data.size();
            if (result.data.empty() || result.data.size() < options_.batch_size)
                exhausted_ = true;
            co_return result;
        }

        /**
         * @brief Fetches the next batch and observes a cooperative cancellation token.
         */
        auto next(cancel_token& cancellation) -> task<model_result<T>>
        {
            if (cancellation.is_cancelled())
            {
                model_result<T> result;
                result.error_msg = "cursor cancelled";
                result.framework_error = std::make_error_code(
                    std::errc::operation_canceled);
                co_return result;
            }
            co_return co_await next();
        }

        [[nodiscard]] auto done() const noexcept -> bool
        {
            return exhausted_;
        }

    private:
        database_session* owner_;
        query_wrapper<T> query_;
        cursor_options options_;
        std::size_t offset_{};
        bool exhausted_{};
    };

    explicit database_session(Client& client,
        sql_dialect dialect = sql_dialect::mysql,
        std::shared_ptr<const interceptor_chain> interceptors = {}) noexcept
        : client_(&client), dialect_(dialect), dialect_config_(get_dialect_config(dialect)), interceptors_(std::move(interceptors))
    {
    }

    /**
     * @brief Creates a session pinned to one routed physical table.
     *
     * Model metadata remains unchanged. Every typed CRUD statement generated
     * by this session uses the supplied physical table, while raw SQL methods
     * keep their original behavior.
     */
    database_session(Client& client, std::string physical_table,
        sql_dialect dialect = sql_dialect::mysql,
        std::shared_ptr<const interceptor_chain> interceptors = {})
        : client_(&client), dialect_(dialect), dialect_config_(get_dialect_config(dialect)), physical_table_(std::move(physical_table)), interceptors_(std::move(interceptors))
    {
    }

    [[nodiscard]] auto underlying() noexcept -> Client&
    {
        return *client_;
    }

    auto query(std::string_view sql) -> task<query_result>
    {
        auto statement = prepare_statement(std::string{sql}, {},
            classify_operation(sql));
        if (!statement)
        {
            query_result rejected;
            rejected.error_msg = statement.error();
            co_return rejected;
        }
        if (statement->args.empty())
            co_return adapt(co_await client_->query(statement->query));
        co_return co_await execute_prepared(std::move(*statement));
    }

    auto execute(std::string_view sql) -> task<query_result>
    {
        auto statement = prepare_statement(std::string{sql}, {},
            classify_operation(sql));
        if (!statement)
        {
            query_result rejected;
            rejected.error_msg = statement.error();
            co_return rejected;
        }
        if (statement->args.empty())
            co_return adapt(co_await client_->execute(statement->query));
        co_return co_await execute_prepared(std::move(*statement));
    }

    auto execute(parameterized_query query) -> task<query_result>
    {
        // Every protocol adapter accepts the ORM's common parameter object.
        // PostgreSQL sends it as a bound extended query; MySQL renders it with
        // its connection charset/escaping rules before COM_QUERY.  Keeping
        // that conversion at the protocol edge avoids both SQL interpolation
        // in repositories and dialect-specific client knowledge here.
        const auto operation = classify_operation(query.query);
        auto statement = prepare_statement(std::move(query.query),
            std::move(query.args), operation);
        if (!statement)
        {
            query_result rejected;
            rejected.error_msg = statement.error();
            co_return rejected;
        }
        co_return co_await execute_prepared(std::move(*statement));
    }

    [[nodiscard]] auto dialect() const noexcept -> sql_dialect
    {
        return dialect_;
    }

    /**
     * @brief Builds an intercepted SELECT statement without executing it.
     *
     * Protocol-specific streaming adapters use this boundary to share routed
     * table names, dialect placeholders and the session interceptor chain. The
     * returned object owns both SQL text and bind values across suspension.
     */
    template <Model T>
    auto prepare_select(const query_wrapper<T>& query) const
        -> std::expected<parameterized_query, std::string>
    {
        auto [sql, parameters] = query.build_select_sql(
            table_name<T>(), dialect_config_);
        return prepare_statement(std::move(sql), std::move(parameters),
            sql_operation::query);
    }

    /**
     * @brief Opens a stateful cursor over a typed query.
     */
    template <Model T>
    auto open_cursor(query_wrapper<T> query = {}, cursor_options options = {})
        -> cursor<T>
    {
        options.batch_size = std::max<std::size_t>(1, options.batch_size);
        return cursor<T>{*this, std::move(query), options};
    }

    /**
     * @brief Installs the standard tenant, logical-delete and SQL-safety chain.
     *
     * Installation is explicit so low-level sessions retain their historical
     * behavior. Application repositories and gateways can call this once at
     * composition time; all subsequent typed and raw operations share the
     * same frozen chain. Model-stage field filling and optimistic locking are
     * installed from the same options so disabling a policy is effective for
     * every operation performed by this session.
     */
    template <Model T>
    auto enable_automatic_interceptors(automatic_interceptor_options options = {})
        -> std::expected<void, std::string>
    {
        auto chain = make_automatic_interceptor_chain<T>(options);
        if (!chain)
            return std::unexpected(chain.error());
        interceptors_ = *chain;
        field_fill_enabled_ = options.field_fill;
        optimistic_lock_enabled_ = options.optimistic_lock;
        return {};
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
        query.eq(primary_key->col.column_name, id);
        co_return co_await find_one(query, single_result_policy::first);
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
        query.eq(field->col.column_name, value);
        co_return co_await find_one(query);
    }

    /**
     * @brief Finds one model while enforcing an explicit cardinality policy.
     *
     * The strict policy requests at most two rows, which detects ambiguous
     * results without materializing the entire result set. Database diagnostics
     * are preserved unchanged. An ambiguity is reported through
     * `framework_error` and never masquerades as an empty result.
     */
    template <Model T>
    auto find_one(const query_wrapper<T>& query,
        single_result_policy policy = single_result_policy::require_unique)
        -> task<model_result<T>>
    {
        auto bounded = query;
        bounded.limit(policy == single_result_policy::require_unique ? 2 : 1);
        auto result = co_await find(bounded);
        if (result.is_err())
            co_return result;

        if (policy == single_result_policy::require_unique && result.data.size() > 1)
        {
            result.data.clear();
            result.error_msg = "single-row query matched more than one row";
            result.framework_error = std::make_error_code(std::errc::result_out_of_range);
        }
        co_return result;
    }

    /**
     * @brief Finds the first matching model without asserting uniqueness.
     */
    template <Model T>
    auto find_first(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        co_return co_await find_one(query, single_result_policy::first);
    }

    /**
     * @brief Consumes rows in bounded pages with cooperative backpressure.
     *
     * The next page is not requested until the handler completes, keeping
     * memory bounded for clients without a wire-level cursor.
     */
    template <Model T>
    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options = {}) -> task<std::expected<void, std::string>>
    {
        if (!handler || options.batch_size == 0)
            co_return std::unexpected("stream handler and batch_size are required");
        std::size_t offset = 0;
        std::size_t delivered = 0;
        while (delivered < options.max_rows)
        {
            if (options.deadline && std::chrono::steady_clock::now() >= *options.deadline)
                co_return std::unexpected("row stream deadline exceeded");
            auto page_query = query;
            const auto remaining = options.max_rows - delivered;
            page_query.limit(static_cast<std::int64_t>(
                std::min(options.batch_size, remaining)));
            page_query.offset(static_cast<std::int64_t>(offset));
            auto page_result = co_await find(page_query);
            if (page_result.is_err())
                co_return std::unexpected(page_result.error_msg);
            if (page_result.data.empty())
                break;
            for (const auto& row : page_result.data)
            {
                if (options.deadline && std::chrono::steady_clock::now() >= *options.deadline)
                    co_return std::unexpected("row stream deadline exceeded");
                auto handled = co_await handler(row);
                if (!handled)
                    co_return handled;
                ++delivered;
                if (delivered >= options.max_rows)
                    break;
            }
            offset += page_result.data.size();
            if (page_result.data.size() < options.batch_size)
                break;
        }
        co_return std::expected<void, std::string>{};
    }

    /**
     * @brief Streams dynamic projection maps in bounded pages.
     */
    template <Model T>
    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)> handler,
        stream_options options = {}) -> task<std::expected<void, std::string>>
    {
        if (!handler || options.batch_size == 0)
            co_return std::unexpected("projection handler and batch_size are required");
        std::size_t offset{};
        std::size_t delivered{};
        while (delivered < options.max_rows)
        {
            if (options.deadline &&
                std::chrono::steady_clock::now() >= *options.deadline)
                co_return std::unexpected("projection stream deadline exceeded");
            auto page_query = query;
            const auto remaining = options.max_rows - delivered;
            page_query.limit(static_cast<std::int64_t>(
                std::min(options.batch_size, remaining)));
            page_query.offset(static_cast<std::int64_t>(offset));
            auto page = co_await select_maps(page_query);
            if (page.is_err())
                co_return std::unexpected(page.error_msg);
            if (page.data.empty())
                break;
            for (const auto& row : page.data)
            {
                if (options.deadline &&
                    std::chrono::steady_clock::now() >= *options.deadline)
                    co_return std::unexpected("projection stream deadline exceeded");
                auto handled = co_await handler(row);
                if (!handled)
                    co_return handled;
                if (++delivered >= options.max_rows)
                    break;
            }
            offset += page.data.size();
            if (page.data.size() < options.batch_size)
                break;
        }
        co_return std::expected<void, std::string>{};
    }

    /**
     * @brief Streams dynamic projection maps with cooperative cancellation.
     */
    template <Model T>
    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)> handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        if (!handler || options.batch_size == 0)
            co_return std::unexpected("projection handler and batch_size are required");
        std::size_t offset{};
        std::size_t delivered{};
        while (delivered < options.max_rows)
        {
            if (cancellation.is_cancelled())
                co_return std::unexpected("projection stream cancelled");
            if (options.deadline &&
                std::chrono::steady_clock::now() >= *options.deadline)
                co_return std::unexpected("projection stream deadline exceeded");
            auto page_query = query;
            const auto remaining = options.max_rows - delivered;
            page_query.limit(static_cast<std::int64_t>(
                std::min(options.batch_size, remaining)));
            page_query.offset(static_cast<std::int64_t>(offset));
            auto page = co_await select_maps(page_query);
            if (page.is_err())
                co_return std::unexpected(page.error_msg);
            if (page.data.empty())
                break;
            for (const auto& row : page.data)
            {
                if (cancellation.is_cancelled())
                    co_return std::unexpected("projection stream cancelled");
                if (options.deadline &&
                    std::chrono::steady_clock::now() >= *options.deadline)
                    co_return std::unexpected("projection stream deadline exceeded");
                auto handled = co_await handler(row);
                if (!handled)
                    co_return handled;
                if (++delivered >= options.max_rows)
                    break;
            }
            offset += page.data.size();
            if (page.data.size() < options.batch_size)
                break;
        }
        co_return std::expected<void, std::string>{};
    }

    /**
     * @brief Cancellable overload of the bounded row stream.
     */
    template <Model T>
    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        if (!handler || options.batch_size == 0)
            co_return std::unexpected("stream handler and batch_size are required");
        std::size_t offset = 0;
        std::size_t delivered = 0;
        while (delivered < options.max_rows)
        {
            if (cancellation.is_cancelled())
                co_return std::unexpected("row stream cancelled");
            if (options.deadline && std::chrono::steady_clock::now() >= *options.deadline)
                co_return std::unexpected("row stream deadline exceeded");
            auto page_query = query;
            const auto remaining = options.max_rows - delivered;
            page_query.limit(static_cast<std::int64_t>(
                std::min(options.batch_size, remaining)));
            page_query.offset(static_cast<std::int64_t>(offset));
            auto page_result = co_await find(page_query);
            if (page_result.is_err())
                co_return std::unexpected(page_result.error_msg);
            if (page_result.data.empty())
                break;
            for (const auto& row : page_result.data)
            {
                if (cancellation.is_cancelled())
                    co_return std::unexpected("row stream cancelled");
                if (options.deadline && std::chrono::steady_clock::now() >= *options.deadline)
                    co_return std::unexpected("row stream deadline exceeded");
                auto handled = co_await handler(row);
                if (!handled)
                    co_return handled;
                ++delivered;
                if (delivered >= options.max_rows)
                    break;
            }
            offset += page_result.data.size();
            if (page_result.data.size() < options.batch_size)
                break;
        }
        co_return std::expected<void, std::string>{};
    }

    /**
     * @brief Finds models whose primary keys occur in @p ids.
     */
    template <Model T, typename Id>
    auto find_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        if (ids.empty())
            co_return model_result<T>{};

        const auto* primary_key = model_traits<T>::meta().pk();
        if (!primary_key)
            co_return failure<T>("model has no primary key");

        query_wrapper<T> query;
        query.in(primary_key->col.column_name, normalize_values(ids));
        co_return co_await find(query);
    }

    /**
     * @brief Finds models matching a set of mapped column equalities.
     *
     * Unknown columns are rejected before I/O. Null values use `IS NULL`.
     */
    template <Model T>
    auto find_by_map(std::span<const std::pair<std::string, param_value>> values)
        -> task<model_result<T>>
    {
        if (auto error = validate_columns<T>(values))
            co_return failure<T>(*error, std::make_error_code(std::errc::invalid_argument));

        query_wrapper<T> query;
        query.all_eq(values);
        co_return co_await find(query);
    }

    /**
     * @brief Tests whether at least one row matches while preserving errors.
     */
    template <Model T>
    auto exists(const query_wrapper<T>& query) -> task<model_result<bool>>
    {
        auto found = co_await find_first(query);
        model_result<bool> result;
        result.affected_rows = found.affected_rows;
        result.last_insert_id = found.last_insert_id;
        result.error_msg = std::move(found.error_msg);
        result.sql_state = std::move(found.sql_state);
        result.error_code = found.error_code;
        result.framework_error = found.framework_error;
        if (result.ok())
            result.data.push_back(!found.data.empty());
        co_return result;
    }

    /**
     * @brief Executes a projection and returns each row as a named field map.
     */
    template <Model T>
    auto select_maps(const query_wrapper<T>& query) -> task<model_result<projection_row>>
    {
        auto [sql, parameters] = query.build_select_sql(table_name<T>(), dialect_config_);
        auto raw = co_await execute_bound(std::move(sql), std::move(parameters));
        co_return map_projection_rows(std::move(raw));
    }

    /**
     * @brief Returns the first projected column from every matching row.
     */
    template <Model T>
    auto select_objects(const query_wrapper<T>& query) -> task<model_result<field_value>>
    {
        auto [sql, parameters] = query.build_select_sql(table_name<T>(), dialect_config_);
        auto raw = co_await execute_bound(std::move(sql), std::move(parameters));
        auto result = diagnostics<field_value>(raw);
        if (result.is_err())
            co_return result;

        result.data.reserve(raw.rows.size());
        for (auto& row : raw.rows)
            if (!row.empty())
                result.data.push_back(std::move(row.front()));
        co_return result;
    }

    /**
     * @brief Executes a projection page while retaining count/select failures.
     */
    template <Model T>
    auto page_maps(std::size_t page, std::size_t page_size,
        const query_wrapper<T>& query = {}) -> task<page_result<projection_row>>
    {
        page_result<projection_row> result;
        result.page = std::max<std::size_t>(1, page);
        result.page_size = std::max<std::size_t>(1, page_size);

        auto [count_sql, count_parameters] = query.build_count_sql(
            table_name<T>(), dialect_config_);
        auto count_result = co_await execute_bound(
            std::move(count_sql), std::move(count_parameters));
        result.records = diagnostics<projection_row>(count_result);
        if (result.records.is_err())
            co_return result;
        if (!count_result.rows.empty() && !count_result.rows.front().empty())
        {
            const auto& value = count_result.rows.front().front();
            if (value.is_uint64())
                result.total = static_cast<std::size_t>(value.get_uint64());
            else if (value.is_int64() && value.get_int64() >= 0)
                result.total = static_cast<std::size_t>(value.get_int64());
            else
            {
                result.records = failure<projection_row>(
                    "count query returned a non-integral value",
                    std::make_error_code(std::errc::illegal_byte_sequence));
                co_return result;
            }
        }

        result.total_pages = result.total == 0
            ? 0
            : (result.total + result.page_size - 1) / result.page_size;
        if (result.total == 0 || result.page > result.total_pages)
            co_return result;

        auto bounded = query;
        bounded.limit(static_cast<std::int64_t>(result.page_size))
            .offset(static_cast<std::int64_t>((result.page - 1) * result.page_size));
        result.records = co_await select_maps(bounded);
        co_return result;
    }

    /**
     * @brief Executes a typed model page with count and query diagnostics.
     */
    template <Model T>
    auto page(std::size_t page, std::size_t page_size,
        const query_wrapper<T>& query = {}) -> task<page_result<T>>
    {
        page_result<T> result;
        result.page = std::max<std::size_t>(1, page);
        result.page_size = std::max<std::size_t>(1, page_size);

        auto [count_sql, count_parameters] = query.build_count_sql(
            table_name<T>(), dialect_config_);
        auto count_result = co_await execute_bound(
            std::move(count_sql), std::move(count_parameters));
        result.records = diagnostics<T>(count_result);
        if (result.records.is_err())
            co_return result;
        if (!count_result.rows.empty() && !count_result.rows.front().empty())
        {
            const auto& value = count_result.rows.front().front();
            if (value.is_uint64())
                result.total = static_cast<std::size_t>(value.get_uint64());
            else if (value.is_int64() && value.get_int64() >= 0)
                result.total = static_cast<std::size_t>(value.get_int64());
            else
            {
                result.records = failure<T>(
                    "count query returned a non-integral value",
                    std::make_error_code(std::errc::illegal_byte_sequence));
                co_return result;
            }
        }

        result.total_pages = result.total == 0
            ? 0
            : (result.total + result.page_size - 1) / result.page_size;
        if (result.total == 0 || result.page > result.total_pages)
            co_return result;

        auto bounded = query;
        bounded.limit(static_cast<std::int64_t>(result.page_size))
            .offset(static_cast<std::int64_t>((result.page - 1) * result.page_size));
        result.records = co_await find(bounded);
        co_return result;
    }

    template <Model T> auto insert(T& model) -> task<model_result<T>>
    {
        if (field_fill_enabled_)
            global_auto_fill_interceptor().template fill_insert_fields<T>(model);
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

    /**
     * @brief Inserts or updates a model using the database native upsert form.
     *
     * MySQL uses `ON DUPLICATE KEY UPDATE`; PostgreSQL uses `ON CONFLICT`.
     * Values remain bound parameters and database diagnostics are preserved.
     */
    template <Model T> auto upsert(T& model) -> task<model_result<T>>
    {
        const auto& meta = model_traits<T>::meta();
        const auto* primary_key = meta.pk();
        const auto fields = meta.insertable_fields();
        if (!primary_key)
            co_return failure<T>("upsert requires a primary key");
        if (fields.empty())
            co_return failure<T>("model has no insertable fields");

        std::string sql = "INSERT INTO ";
        sql += quote_identifier(table_name<T>(), dialect_config_) + " (";
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

        const auto updates = meta.updatable_fields();
        if (dialect_ == sql_dialect::postgresql)
        {
            sql += " ON CONFLICT (" +
                quote_identifier(primary_key->col.column_name, dialect_config_) + ") DO ";
            if (updates.empty())
                sql += "NOTHING";
            else
            {
                sql += "UPDATE SET ";
                for (std::size_t index = 0; index < updates.size(); ++index)
                {
                    if (index != 0)
                        sql += ", ";
                    const auto name = quote_identifier(updates[index]->col.column_name,
                        dialect_config_);
                    sql += name + " = EXCLUDED." + name;
                }
            }
            sql += " RETURNING *";
        }
        else
        {
            sql += " ON DUPLICATE KEY UPDATE ";
            if (updates.empty())
                sql += quote_identifier(primary_key->col.column_name, dialect_config_) +
                    " = " + quote_identifier(primary_key->col.column_name, dialect_config_);
            else
            {
                for (std::size_t index = 0; index < updates.size(); ++index)
                {
                    if (index != 0)
                        sql += ", ";
                    const auto name = quote_identifier(updates[index]->col.column_name,
                        dialect_config_);
                    sql += name + " = VALUES(" + name + ")";
                }
            }
        }

        auto result = map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
        if (result.is_err())
            co_return result;
        if (!result.data.empty())
            model = result.data.front();
        else
        {
            fill_insert_id<T>(model, result.last_insert_id);
            result.data.push_back(model);
        }
        co_return result;
    }

    /**
     * @brief Executes native upserts in one transaction.
     */
    template <Model T>
    auto upsert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        if (models.empty())
            co_return model_result<T>{};
        if (batch_size == 0)
            co_return failure<T>("batch_size must be greater than zero",
                std::make_error_code(std::errc::invalid_argument));
        auto started = co_await begin_expected_transaction(std::nullopt);
        if (started.is_err())
            co_return map<T>(std::move(started));

        model_result<T> aggregate;
        for (std::size_t offset = 0; offset < models.size(); offset += batch_size)
        {
            const auto end = std::min(models.size(), offset + batch_size);
            for (std::size_t index = offset; index < end; ++index)
            {
                auto current = co_await upsert(models[index]);
                if (current.is_err())
                {
                    current.batch_index = offset / batch_size;
                    current.item_index = index;
                    current.operation = "upsert";
                    co_return co_await rollback_batch_failure(std::move(current));
                }
                aggregate.affected_rows += current.affected_rows;
                aggregate.last_insert_id = current.last_insert_id != 0
                    ? current.last_insert_id
                    : aggregate.last_insert_id;
                aggregate.data.insert(aggregate.data.end(),
                    std::make_move_iterator(current.data.begin()),
                    std::make_move_iterator(current.data.end()));
            }
        }
        auto committed = co_await execute("COMMIT");
        if (committed.is_err())
            co_return co_await rollback_batch_failure(map<T>(std::move(committed)));
        co_return aggregate;
    }

    template <Model T> auto update(T& model) -> task<model_result<T>>
    {
        if (field_fill_enabled_)
            global_auto_fill_interceptor().template fill_update_fields<T>(model);
        auto result = co_await update(static_cast<const T&>(model));
        if (optimistic_lock_enabled_ && result.ok() && result.affected_rows != 0)
        {
            const auto& meta = model_traits<T>::meta();
            for (const auto& field : meta.fields)
            {
                if (field.col.column_name != "version" &&
                    field.col.column_name != "Version" &&
                    !has_flag(field.col.flags, col_flag::version))
                    continue;
                const auto value = field.getter(model);
                if (value.kind == param_value::kind_t::int64_kind)
                    field.setter(model, field_value::from_int64(value.int_val + 1));
                else if (value.kind == param_value::kind_t::uint64_kind)
                    field.setter(model, field_value::from_uint64(value.uint_val + 1));
                break;
            }
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

        const field_mapping<T>* version_field = nullptr;
        if (optimistic_lock_enabled_)
        {
            for (const auto& field : meta.fields)
            {
                if (field.col.column_name == "version" ||
                    field.col.column_name == "Version" ||
                    has_flag(field.col.flags, col_flag::version))
                {
                    version_field = &field;
                    break;
                }
            }
        }

        std::string sql = "UPDATE ";
        sql += quote_identifier(table_name<T>(), dialect_config_);
        sql += " SET ";
        std::vector<param_value> parameters;
        parameters.reserve(fields.size() + (version_field ? 1 : 0) + 1);
        std::size_t assignment_count = 0;
        for (const auto* field : fields)
        {
            if (version_field && field->col.column_name == version_field->col.column_name)
                continue;
            if (assignment_count != 0)
                sql += ", ";
            sql += quote_identifier(field->col.column_name, dialect_config_);
            sql += " = ";
            sql += make_placeholder(static_cast<int>(parameters.size() + 1),
                dialect_config_);
            parameters.push_back(field->getter(model));
            ++assignment_count;
        }
        if (version_field)
        {
            if (assignment_count != 0)
                sql += ", ";
            const auto version_name = quote_identifier(version_field->col.column_name,
                dialect_config_);
            sql += version_name + " = " + version_name + " + 1";
        }
        if (assignment_count == 0 && !version_field)
            co_return failure<T>("model has no updatable fields");
        parameters.push_back(primary_key->getter(model));
        sql += " WHERE ";
        sql += quote_identifier(primary_key->col.column_name, dialect_config_);
        sql += " = ";
        sql += make_placeholder(static_cast<int>(parameters.size()), dialect_config_);
        if (version_field)
        {
            sql += " AND " + quote_identifier(version_field->col.column_name,
                dialect_config_) + " = ";
            sql += make_placeholder(static_cast<int>(parameters.size() + 1),
                dialect_config_);
            parameters.push_back(version_field->getter(model));
        }
        if (dialect_config_.supports_returning)
            sql += " RETURNING *";
        auto result = map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
        if (version_field && result.ok() && result.affected_rows == 0)
        {
            result.error_msg = "optimistic lock conflict";
            result.framework_error = std::make_error_code(
                std::errc::state_not_recoverable);
        }
        co_return result;
    }

    /**
     * @brief Inserts a missing model or updates the row identified by its key.
     *
     * Auto-increment keys with an unset value are inserted directly. Other
     * keys are checked on the same session before choosing INSERT or UPDATE.
     */
    template <Model T> auto save_or_update(T& model) -> task<model_result<T>>
    {
        const auto* primary_key = model_traits<T>::meta().pk();
        if (!primary_key)
            co_return failure<T>("model has no primary key");

        auto key = primary_key->getter(model);
        if (primary_key->col.is_auto() && is_unset_key(key))
            co_return co_await insert(model);

        auto existing = co_await find_by_id<T>(key);
        if (existing.is_err())
            co_return existing;
        if (existing.data.empty())
            co_return co_await insert(model);
        co_return co_await update(model);
    }

    /**
     * @brief Updates models by primary key in one transaction.
     */
    template <Model T>
    auto update_batch_by_id(std::span<const T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        if (models.empty())
            co_return model_result<T>{};
        if (batch_size == 0)
        {
            co_return failure<T>("batch_size must be greater than zero",
                std::make_error_code(std::errc::invalid_argument));
        }

        auto started = co_await begin_expected_transaction(std::nullopt);
        if (started.is_err())
            co_return map<T>(std::move(started));

        model_result<T> aggregate;
        for (std::size_t offset = 0; offset < models.size(); offset += batch_size)
        {
            const auto end = std::min(models.size(), offset + batch_size);
            for (std::size_t index = offset; index < end; ++index)
            {
                auto updated = co_await update(models[index]);
                if (updated.is_err())
                {
                    updated.batch_index = offset / batch_size;
                    updated.item_index = index;
                    updated.operation = "update_batch_by_id";
                    co_return co_await rollback_batch_failure(std::move(updated));
                }
                aggregate.affected_rows += updated.affected_rows;
                aggregate.data.insert(aggregate.data.end(),
                    std::make_move_iterator(updated.data.begin()),
                    std::make_move_iterator(updated.data.end()));
            }
        }

        auto committed = co_await execute("COMMIT");
        if (committed.is_err())
            co_return co_await rollback_batch_failure(map<T>(std::move(committed)));
        co_return aggregate;
    }

    /**
     * @brief Saves or updates models atomically on the current session.
     */
    template <Model T>
    auto save_or_update_batch(std::span<T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        if (models.empty())
            co_return model_result<T>{};
        if (batch_size == 0)
        {
            co_return failure<T>("batch_size must be greater than zero",
                std::make_error_code(std::errc::invalid_argument));
        }

        auto started = co_await begin_expected_transaction(std::nullopt);
        if (started.is_err())
            co_return map<T>(std::move(started));

        model_result<T> aggregate;
        for (std::size_t offset = 0; offset < models.size(); offset += batch_size)
        {
            const auto end = std::min(models.size(), offset + batch_size);
            for (std::size_t index = offset; index < end; ++index)
            {
                auto saved = co_await save_or_update(models[index]);
                if (saved.is_err())
                {
                    saved.batch_index = offset / batch_size;
                    saved.item_index = index;
                    saved.operation = "save_or_update_batch";
                    co_return co_await rollback_batch_failure(std::move(saved));
                }
                aggregate.affected_rows += saved.affected_rows;
                aggregate.last_insert_id = saved.last_insert_id != 0
                    ? saved.last_insert_id
                    : aggregate.last_insert_id;
                aggregate.data.insert(aggregate.data.end(),
                    std::make_move_iterator(saved.data.begin()),
                    std::make_move_iterator(saved.data.end()));
            }
        }

        auto committed = co_await execute("COMMIT");
        if (committed.is_err())
            co_return co_await rollback_batch_failure(map<T>(std::move(committed)));
        co_return aggregate;
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

    /**
     * @brief Removes rows whose primary keys occur in @p ids.
     */
    template <Model T, typename Id>
    auto remove_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        if (ids.empty())
            co_return model_result<T>{};
        const auto* primary_key = model_traits<T>::meta().pk();
        if (!primary_key)
            co_return failure<T>("model has no primary key");

        query_wrapper<T> query;
        query.in(primary_key->col.column_name, normalize_values(ids));
        co_return co_await remove(query);
    }

    /**
     * @brief Removes rows matching mapped column equalities.
     */
    template <Model T>
    auto remove_by_map(std::span<const std::pair<std::string, param_value>> values)
        -> task<model_result<T>>
    {
        if (values.empty())
        {
            co_return failure<T>(
                "empty map delete requires the allow_full_table tag",
                std::make_error_code(std::errc::operation_not_permitted));
        }
        if (auto error = validate_columns<T>(values))
            co_return failure<T>(*error, std::make_error_code(std::errc::invalid_argument));

        query_wrapper<T> query;
        query.all_eq(values);
        co_return co_await remove(query);
    }

    template <Model T> auto find(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        auto statement = prepare_select<T>(query);
        if (!statement)
            co_return failure<T>(statement.error());
        co_return map<T>(co_await execute_prepared(std::move(*statement)));
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

    /**
     * @brief Counts rows while preserving native database diagnostics.
     */
    template <Model T>
    auto count_result(const query_wrapper<T>& query)
        -> task<model_result<std::int64_t>>
    {
        auto [sql, parameters] = query.build_count_sql(table_name<T>(), dialect_config_);
        auto raw = co_await execute_bound(std::move(sql), std::move(parameters));
        model_result<std::int64_t> result = diagnostics<std::int64_t>(raw);
        if (result.is_err())
            co_return result;
        if (raw.rows.empty() || raw.rows.front().empty())
            co_return result;
        const auto& value = raw.rows.front().front();
        if (value.is_uint64())
            result.data.push_back(static_cast<std::int64_t>(value.get_uint64()));
        else if (value.is_int64())
            result.data.push_back(value.get_int64());
        else
            result = failure<std::int64_t>(
                "count query returned a non-integral value",
                std::make_error_code(std::errc::illegal_byte_sequence));
        co_return result;
    }

    /**
     * @brief Inserts models in transactionally bounded batches.
     *
     * A failed item rolls the active transaction back and reports both its
     * batch and absolute item position in the returned result.
     */
    template <Model T>
    auto insert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        if (models.empty())
            co_return model_result<T>{};
        if (batch_size == 0)
            co_return failure<T>("batch_size must be greater than zero",
                std::make_error_code(std::errc::invalid_argument));

        model_result<T> aggregate;
        for (std::size_t offset = 0; offset < models.size(); offset += batch_size)
        {
            const auto end = std::min(models.size(), offset + batch_size);
            auto started = co_await begin_expected_transaction(std::nullopt);
            if (started.is_err())
                co_return map<T>(std::move(started));
            for (std::size_t index = offset; index < end; ++index)
            {
                auto inserted = co_await insert(models[index]);
                if (inserted.is_err())
                {
                    inserted.batch_index = offset / batch_size;
                    inserted.item_index = index;
                    inserted.operation = "insert_batch";
                    co_return co_await rollback_batch_failure(std::move(inserted));
                }
                aggregate.affected_rows += inserted.affected_rows;
                aggregate.last_insert_id = inserted.last_insert_id != 0
                    ? inserted.last_insert_id
                    : aggregate.last_insert_id;
                aggregate.data.insert(aggregate.data.end(),
                    std::make_move_iterator(inserted.data.begin()),
                    std::make_move_iterator(inserted.data.end()));
            }
            auto committed = co_await execute("COMMIT");
            if (committed.is_err())
            {
                auto failure_result = map<T>(std::move(committed));
                failure_result.batch_index = offset / batch_size;
                failure_result.operation = "insert_batch";
                co_return co_await rollback_batch_failure(std::move(failure_result));
            }
        }
        co_return aggregate;
    }

    template <Model T> auto remove(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        if (query.is_empty())
        {
            co_return failure<T>(
                "full-table delete requires the allow_full_table tag",
                std::make_error_code(std::errc::operation_not_permitted));
        }
        co_return co_await remove(query, allow_full_table);
    }

    /**
     * @brief Executes DELETE with explicit authorization for an empty filter.
     */
    template <Model T>
    auto remove(const query_wrapper<T>& query, allow_full_table_t)
        -> task<model_result<T>>
    {
        auto [sql, parameters] = query.build_delete_sql(table_name<T>(), dialect_config_);
        if (dialect_config_.supports_returning)
            sql += " RETURNING *";
        co_return map<T>(co_await execute_bound(std::move(sql), std::move(parameters)));
    }

    template <Model T> auto update(const update_wrapper<T>& update) -> task<model_result<T>>
    {
        if (!update.has_conditions())
        {
            co_return failure<T>(
                "full-table update requires the allow_full_table tag",
                std::make_error_code(std::errc::operation_not_permitted));
        }
        co_return co_await this->update(update, allow_full_table);
    }

    /**
     * @brief Executes UPDATE with explicit authorization for an empty filter.
     */
    template <Model T>
    auto update(const update_wrapper<T>& update, allow_full_table_t)
        -> task<model_result<T>>
    {
        if (!update.has_assignments())
        {
            co_return failure<T>("update has no assignments",
                std::make_error_code(std::errc::invalid_argument));
        }
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
        return observe_sql(std::string{sql}, parent, on_end, options, false);
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
        return observe_sql(std::string{sql}, parent, on_end, options, true);
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
                return execute_owned(std::move(sql), execution);
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
                result = co_await execute(std::move(sql));
            else
                result = execution ? co_await execute(sql) : co_await query(sql);
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

    /**
     * @brief Keeps raw SQL alive while a lazy fallback operation executes.
     *
     * Observation setup can fail before its coroutine frame takes ownership.
     * This coroutine owns the SQL string so the string_view accepted by the
     * public raw methods cannot outlive its storage.
     */
    auto execute_owned(std::string sql, bool execution) -> task<query_result>
    {
        if (execution)
            co_return co_await execute(std::string_view{sql});
        co_return co_await query(std::string_view{sql});
    }

    [[nodiscard]] auto prepare_statement(std::string sql,
        std::vector<param_value> parameters, sql_operation operation) const
        -> std::expected<parameterized_query, std::string>
    {
        if (interceptors_ && !interceptors_->empty())
        {
            auto intercepted = interceptors_->apply(operation,
                intercepted_statement{std::move(sql), std::move(parameters)});
            if (!intercepted)
                return std::unexpected(intercepted.error());
            sql = std::move(intercepted->sql);
            parameters = std::move(intercepted->parameters);
        }
        return parameterized_query{std::move(sql), std::move(parameters)};
    }

    auto execute_prepared(parameterized_query statement) -> task<query_result>
    {
        co_return adapt(co_await client_->execute(std::move(statement)));
    }

    [[nodiscard]] static auto classify_operation(std::string_view sql) noexcept
        -> sql_operation
    {
        while (!sql.empty() && std::isspace(static_cast<unsigned char>(sql.front())))
            sql.remove_prefix(1);
        if (sql.starts_with("SELECT") || sql.starts_with("WITH"))
            return sql_operation::query;
        if (sql.starts_with("INSERT"))
            return sql_operation::insert;
        if (sql.starts_with("UPDATE"))
            return sql_operation::update;
        if (sql.starts_with("DELETE"))
            return sql_operation::remove;
        return sql_operation::execute;
    }

    [[nodiscard]] static auto is_unset_key(const param_value& value) noexcept -> bool
    {
        switch (value.kind)
        {
        case param_value::kind_t::null_kind:
            return true;
        case param_value::kind_t::int64_kind:
            return value.int_val == 0;
        case param_value::kind_t::uint64_kind:
            return value.uint_val == 0;
        case param_value::kind_t::string_kind:
            return value.str_val.empty();
        default:
            return false;
        }
    }

    template <typename Value>
    static auto normalize_values(std::span<const Value> values)
        -> std::vector<param_value>
    {
        std::vector<param_value> normalized;
        normalized.reserve(values.size());
        for (const auto& value : values)
        {
            if constexpr (std::integral<Value> && std::is_signed_v<Value>)
                normalized.push_back(param_value::from_int(
                    static_cast<std::int64_t>(value)));
            else if constexpr (std::integral<Value>)
                normalized.push_back(param_value::from_uint(
                    static_cast<std::uint64_t>(value)));
            else
                normalized.push_back(to_query_parameter(value));
        }
        return normalized;
    }

    template <Model T>
    static auto validate_columns(
        std::span<const std::pair<std::string, param_value>> values)
        -> std::optional<std::string>
    {
        const auto& metadata = model_traits<T>::meta();
        for (const auto& [column, value] : values)
        {
            static_cast<void>(value);
            if (!metadata.find_column(column))
            {
                return std::format("model '{}' has no mapped column '{}'",
                    metadata.table_name, column);
            }
        }
        return std::nullopt;
    }

    template <class T>
    static auto diagnostics(const query_result& source) -> model_result<T>
    {
        model_result<T> result;
        result.affected_rows = source.affected_rows;
        result.last_insert_id = source.last_insert_id;
        result.error_msg = source.error_msg;
        result.sql_state = source.sql_state;
        result.error_code = source.error_code;
        return result;
    }

    static auto map_projection_rows(query_result source)
        -> model_result<projection_row>
    {
        auto result = diagnostics<projection_row>(source);
        if (result.is_err())
            return result;

        result.data.reserve(source.rows.size());
        for (auto& fields : source.rows)
        {
            projection_row row;
            const auto width = std::min(source.columns.size(), fields.size());
            for (std::size_t index = 0; index < width; ++index)
                row.insert_or_assign(source.columns[index].name, std::move(fields[index]));
            result.data.push_back(std::move(row));
        }
        return result;
    }

    template <Model T>
    auto rollback_batch_failure(model_result<T> failure_result)
        -> task<model_result<T>>
    {
        auto rolled_back = co_await execute("ROLLBACK");
        if (rolled_back.is_err())
        {
            if (!failure_result.error_msg.empty())
                failure_result.error_msg += "; ";
            failure_result.error_msg += transaction_phase_error("rollback", rolled_back);
        }
        co_return failure_result;
    }

    template <class T> static auto failure(std::string message,
        std::error_code framework_error = {}) -> model_result<T>
    {
        model_result<T> result;
        result.error_msg = std::move(message);
        result.framework_error = framework_error;
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
    std::shared_ptr<const interceptor_chain> interceptors_;
    bool field_fill_enabled_ = true;
    bool optimistic_lock_enabled_ = true;
};

} // namespace cnetmod::orm
