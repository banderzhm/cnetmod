export module cnetmod.protocol.postgresql:orm;

import std;
export import cnetmod.orm;
import :connection;
import cnetmod.coro.task;

export namespace cnetmod::orm {

using namespace cnetmod::orm;

template <class T> struct postgresql_orm_result
{
    std::vector<T> data;
    std::uint64_t affected_rows{}, last_insert_id{};
    std::string error_msg;
    std::string sql_state;

    [[nodiscard]] auto ok() const noexcept
    {
        return error_msg.empty();
    }

    [[nodiscard]] auto is_err() const noexcept
    {
        return !ok();
    }

    [[nodiscard]] auto first() const -> std::optional<T>
    {
        return data.empty() ? std::nullopt : std::optional<T>(data.front());
    }
};

inline auto quote_identifier(std::string_view name) -> std::string
{
    if (name.empty() || name.find('\0') != std::string_view::npos)
        throw std::invalid_argument("invalid PostgreSQL identifier");
    std::string result{"\""};
    for (char c : name)
    {
        if (c == '\"')
            result += "\"\"";
        else
            result.push_back(c);
    }
    result.push_back('\"');
    return result;
}

class postgresql_session
{
public:
    explicit postgresql_session(cnetmod::postgresql::client& connection) noexcept
        : connection_(&connection) {}

    /// Runs ORM work in the protocol client's transaction state machine. The
    /// callback is owned by this coroutine frame, so temporary coroutine
    /// callables remain alive until commit or rollback completes.
    template <class Function>
    requires std::invocable<Function&> && requires(Function& function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function function) -> task<result_set>
    {
        co_return co_await connection_->transaction(function);
    }

    template <class Function>
    requires std::invocable<Function&> && requires(Function& function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function function, isolation_level isolation)
        -> task<result_set>
    {
        co_return co_await connection_->transaction(function, isolation);
    }

    /// Creates the table described by the model metadata using PostgreSQL
    /// types and constraints. Schema ownership stays with the ORM model.
    template <Model T> auto create_table() -> task<result_set>
    {
        const auto& metadata = model_traits<T>::meta();
        if (metadata.fields.empty())
        {
            result_set error;
            error.error_msg = "cannot create a table for a model without fields";
            co_return error;
        }
        std::string sql = "CREATE TABLE IF NOT EXISTS " +
            quote_identifier(metadata.table_name) + " (";
        for (std::size_t index = 0; index < metadata.fields.size(); ++index)
        {
            const auto& column = metadata.fields[index].col;
            if (index != 0)
                sql += ", ";
            sql += quote_identifier(column.column_name) + " " +
                postgresql_column_type(column);
            if (column.is_pk())
                sql += " PRIMARY KEY";
            if (!column.is_nullable())
                sql += " NOT NULL";
            if (column.is_unique())
                sql += " UNIQUE";
        }
        sql += ")";
        co_return co_await connection_->execute(sql);
    }

    template <Model T> auto find_all() -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        std::string sql = "SELECT * FROM " + quote_identifier(metadata.table_name);
        co_return co_await select_models<T>(sql);
    }

    template <Model T> auto find_by_id(param_value id) -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        const auto* pk = metadata.pk();
        if (!pk)
            co_return failure<T>("model has no primary key");
        std::string sql = "SELECT * FROM " + quote_identifier(metadata.table_name) +
            " WHERE " + quote_identifier(pk->col.column_name) + " = $1 LIMIT 1";
        auto result = co_await connection_->execute(with_params(sql, {std::move(id)}));
        co_return map<T>(std::move(result));
    }

    /// Finds one model by a mapped column. The column must belong to T, so
    /// repository code never has to interpolate identifiers or SQL fragments.
    template <Model T>
    auto find_one_by(std::string_view column, param_value value)
        -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        const auto* field = metadata.find_column(column);
        if (!field)
            co_return failure<T>(std::format(
                "model '{}' has no mapped column '{}'", metadata.table_name, column));
        auto sql = "SELECT * FROM " + quote_identifier(metadata.table_name) +
            " WHERE " + quote_identifier(field->col.column_name) + " = $1 LIMIT 1";
        co_return map<T>(co_await connection_->execute(
            with_params(std::move(sql), {std::move(value)})));
    }

    template <Model T> auto insert(T& model) -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        auto fields = metadata.insertable_fields();
        std::string sql = "INSERT INTO " + quote_identifier(metadata.table_name) + " (";
        std::vector<param_value> parameters;
        parameters.reserve(fields.size());
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (i)
                sql += ", ";
            sql += quote_identifier(fields[i]->col.column_name);
            parameters.push_back(fields[i]->getter(model));
        }
        sql += ") VALUES (";
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (i)
                sql += ", ";
            sql += "$" + std::to_string(i + 1);
        }
        sql += ")";
        sql += " RETURNING *";
        auto result = co_await connection_->execute(with_params(sql, std::move(parameters)));
        auto mapped = map<T>(std::move(result));
        if (!mapped.data.empty())
            model = mapped.data.front();
        co_return mapped;
    }

    /// Inserts a model and returns it. If the mapped unique column already
    /// exists, PostgreSQL returns the existing row without changing business
    /// data. This provides repository-level idempotency without handwritten SQL.
    template <Model T>
    auto insert_or_get(T& model, std::string_view unique_column)
        -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        const auto* unique_field = metadata.find_column(unique_column);
        if (!unique_field)
            co_return failure<T>(std::format(
                "model '{}' has no mapped column '{}'", metadata.table_name,
                unique_column));

        const auto fields = metadata.insertable_fields();
        if (fields.empty())
            co_return failure<T>("cannot insert a model without insertable fields");

        std::string sql = "INSERT INTO " + quote_identifier(metadata.table_name) + " (";
        std::vector<param_value> parameters;
        parameters.reserve(fields.size());
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            if (index != 0)
                sql += ", ";
            sql += quote_identifier(fields[index]->col.column_name);
            parameters.push_back(fields[index]->getter(model));
        }
        sql += ") VALUES (";
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            if (index != 0)
                sql += ", ";
            sql += "$" + std::to_string(index + 1);
        }
        const auto quoted_unique = quote_identifier(unique_field->col.column_name);
        sql += ") ON CONFLICT (" + quoted_unique + ") DO UPDATE SET " +
            quoted_unique + " = EXCLUDED." + quoted_unique + " RETURNING *";
        auto result = map<T>(co_await connection_->execute(
            with_params(std::move(sql), std::move(parameters))));
        if (result.ok() && !result.data.empty())
            model = result.data.front();
        co_return result;
    }

    template <Model T> auto update(const T& model) -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        const auto* pk = metadata.pk();
        if (!pk)
            co_return failure<T>("model has no primary key");
        auto fields = metadata.updatable_fields();
        std::string sql = "UPDATE " + quote_identifier(metadata.table_name) + " SET ";
        std::vector<param_value> parameters;
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (i)
                sql += ", ";
            sql += quote_identifier(fields[i]->col.column_name) + " = $" + std::to_string(i + 1);
            parameters.push_back(fields[i]->getter(model));
        }
        parameters.push_back(pk->getter(model));
        sql += " WHERE " + quote_identifier(pk->col.column_name) + " = $" + std::to_string(parameters.size()) + " RETURNING *";
        co_return map<T>(co_await connection_->execute(with_params(sql, std::move(parameters))));
    }

    /// Updates a model selected by any mapped column and returns the updated row.
    template <Model T>
    auto update_by(const T& model, std::string_view column, param_value value)
        -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        const auto* predicate = metadata.find_column(column);
        if (!predicate)
            co_return failure<T>(std::format(
                "model '{}' has no mapped column '{}'", metadata.table_name, column));
        const auto fields = metadata.updatable_fields();
        if (fields.empty())
            co_return failure<T>("cannot update a model without updatable fields");

        std::string sql = "UPDATE " + quote_identifier(metadata.table_name) + " SET ";
        std::vector<param_value> parameters;
        parameters.reserve(fields.size() + 1);
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            if (index != 0)
                sql += ", ";
            sql += quote_identifier(fields[index]->col.column_name) + " = $" +
                std::to_string(index + 1);
            parameters.push_back(fields[index]->getter(model));
        }
        parameters.push_back(std::move(value));
        sql += " WHERE " + quote_identifier(predicate->col.column_name) + " = $" +
            std::to_string(parameters.size()) + " RETURNING *";
        co_return map<T>(co_await connection_->execute(
            with_params(std::move(sql), std::move(parameters))));
    }

    template <Model T> auto remove(const T& model) -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        const auto* pk = metadata.pk();
        if (!pk)
            co_return failure<T>("model has no primary key");
        auto sql = "DELETE FROM " + quote_identifier(metadata.table_name) + " WHERE " + quote_identifier(pk->col.column_name) + " = $1";
        co_return map<T>(co_await connection_->execute(with_params(sql, {pk->getter(model)})));
    }

    /// Deletes models selected by a mapped column and returns the removed rows.
    template <Model T>
    auto remove_by(std::string_view column, param_value value)
        -> task<postgresql_orm_result<T>>
    {
        const auto& metadata = model_traits<T>::meta();
        const auto* field = metadata.find_column(column);
        if (!field)
            co_return failure<T>(std::format(
                "model '{}' has no mapped column '{}'", metadata.table_name, column));
        auto sql = "DELETE FROM " + quote_identifier(metadata.table_name) +
            " WHERE " + quote_identifier(field->col.column_name) + " = $1 RETURNING *";
        co_return map<T>(co_await connection_->execute(
            with_params(std::move(sql), {std::move(value)})));
    }

    // ── query_wrapper / update_wrapper fluent API overloads ──────────────

    /// SELECT via query_wrapper (fluent API with member-pointer column refs).
    template <Model T>
    auto find(const query_wrapper<T>& qb) -> task<postgresql_orm_result<T>>
    {
        auto [sql, params] = qb.build_select_sql(sql_dialect::postgresql);
        co_return map<T>(co_await connection_->execute(
            with_params(sql, std::move(params))));
    }

    /// DELETE via query_wrapper (fluent API with member-pointer column refs).
    template <Model T>
    auto execute(const query_wrapper<T>& qb) -> task<postgresql_orm_result<T>>
    {
        auto [sql, params] = qb.build_delete_sql(sql_dialect::postgresql);
        co_return map<T>(co_await connection_->execute(
            with_params(sql, std::move(params))));
    }

    /// Partial UPDATE via update_wrapper.
    template <Model T>
    auto execute(const update_wrapper<T>& ub) -> task<postgresql_orm_result<T>>
    {
        auto [sql, params] = ub.build_sql(sql_dialect::postgresql);
        co_return map<T>(co_await connection_->execute(
            with_params(sql, std::move(params))));
    }

    auto raw_query(std::string_view sql) -> task<result_set>
    {
        co_return co_await connection_->query(sql);
    }

    [[nodiscard]] auto underlying() noexcept -> cnetmod::postgresql::client&
    {
        return *connection_;
    }

private:
    cnetmod::postgresql::client* connection_;

    static auto postgresql_column_type(const column_def& column) -> std::string
    {
        if (column.is_auto())
        {
            if (column.type == column_type::smallint)
                return "SMALLSERIAL";
            if (column.type == column_type::bigint)
                return "BIGSERIAL";
            return "SERIAL";
        }
        switch (column.type)
        {
        case column_type::tinyint:
        case column_type::smallint:
            return "SMALLINT";
        case column_type::mediumint:
        case column_type::int_:
            return "INTEGER";
        case column_type::bigint:
            return "BIGINT";
        case column_type::float_:
            return "REAL";
        case column_type::double_:
            return "DOUBLE PRECISION";
        case column_type::decimal:
            return "DECIMAL";
        case column_type::bit:
            return "BIT";
        case column_type::time:
            return "TIME";
        case column_type::date:
            return "DATE";
        case column_type::datetime:
        case column_type::timestamp:
            return "TIMESTAMP";
        case column_type::char_:
            return "CHAR(255)";
        case column_type::varchar:
            return "VARCHAR(255)";
        case column_type::binary:
        case column_type::varbinary:
        case column_type::blob:
            return "BYTEA";
        case column_type::json:
            return "JSONB";
        case column_type::boolean:
            return "BOOLEAN";
        case column_type::uuid:
            return "UUID";
        case column_type::array:
            return "TEXT[]";
        case column_type::text:
        case column_type::enum_:
        case column_type::set:
        case column_type::geometry:
        case column_type::year:
        case column_type::unknown:
            return "TEXT";
        }
        return "TEXT";
    }

    template <class T> static auto failure(std::string message, std::string sql_state = {}) -> postgresql_orm_result<T>
    {
        postgresql_orm_result<T> r;
        r.error_msg = std::move(message);
        r.sql_state = std::move(sql_state);
        return r;
    }

    template <Model T> static auto map(result_set result) -> postgresql_orm_result<T>
    {
        if (result.is_err())
            return failure<T>(result.error_msg, result.sql_state);
        postgresql_orm_result<T> out;
        out.data = from_result_set<T>(result);
        out.affected_rows = result.affected_rows;
        out.last_insert_id = result.last_insert_id;
        out.sql_state = std::move(result.sql_state);
        return out;
    }

    template <Model T> auto select_models(const std::string& sql) -> task<postgresql_orm_result<T>>
    {
        co_return map<T>(co_await connection_->query(sql));
    }
};

} // namespace cnetmod::orm
