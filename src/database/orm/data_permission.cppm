export module cnetmod.orm.data_permission;

import std;
import cnetmod.orm.interceptor_chain;
import cnetmod.orm.model_metadata;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.multi_tenant;

export namespace cnetmod::orm {

/**
 * @brief Immutable request-level row visibility used by the ORM interceptor.
 *
 * The value is owned by a request-scoped repository and is therefore safe
 * across coroutine suspension and executor changes. An empty restricted scope
 * denies all rows rather than silently disabling the policy.
 */
struct data_permission_scope
{
    bool unrestricted = false;
    bool include_owner = false;
    std::int64_t owner_id{};
    std::vector<std::int64_t> partition_ids;
    // nullopt keeps the historical read/write scope; an empty vector denies
    // writes while preserving read visibility.
    std::optional<std::vector<std::int64_t>> writable_partition_ids;
};

/**
 * @brief Injects model-declared row visibility into parameterized SQL.
 */
template <Model T>
class data_permission_interceptor
{
public:
    explicit data_permission_interceptor(data_permission_scope scope,
        sql_dialect dialect = sql_dialect::mysql,
        bool enforce_insert = false, std::string mapped_table = {})
        : scope_(std::move(scope)), dialect_(dialect),
          enforce_insert_(enforce_insert), mapped_table_(std::move(mapped_table))
    {
    }

    [[nodiscard]] auto apply(sql_operation operation,
        intercepted_statement statement) const
        -> std::expected<intercepted_statement, std::string>
    {
        if (scope_.unrestricted || operation == sql_operation::execute)
            return statement;

        const auto& metadata = model_traits<T>::meta();
        const auto table_name = mapped_table_.empty() ? metadata.table_name
            : std::string_view{mapped_table_};
        const column_def* partition = nullptr;
        const column_def* owner = nullptr;
        for (const auto& field : metadata.fields)
        {
            if (has_flag(field.col.flags, col_flag::data_partition))
            {
                if (partition)
                    return std::unexpected(
                        "model declares more than one data partition field");
                partition = &field.col;
            }
            if (has_flag(field.col.flags, col_flag::data_owner))
            {
                if (owner)
                    return std::unexpected(
                        "model declares more than one data owner field");
                owner = &field.col;
            }
        }
        if (!partition && !owner)
            return statement;
        const auto& partitions = operation == sql_operation::query ||
                !scope_.writable_partition_ids
            ? scope_.partition_ids : *scope_.writable_partition_ids;

        if (operation == sql_operation::insert)
        {
            if (!enforce_insert_)
                return statement;
            if (partition)
            {
                auto checked = validate_bound_insert_column(
                    std::move(statement), table_name,
                    partition->column_name, partitions, dialect_);
                if (!checked)
                    return checked;
                statement = std::move(*checked);
            }
            if (owner && scope_.include_owner)
                return validate_bound_insert_column(std::move(statement),
                    table_name, owner->column_name,
                    std::vector<std::int64_t>{scope_.owner_id}, dialect_);
            if (owner && !partition)
                return std::unexpected("data owner INSERT has no authorized owner");
            return statement;
        }

        if (operation == sql_operation::update && enforce_insert_)
        {
            const auto normalized = uppercase(statement.sql);
            const auto set = find_keyword(normalized, "SET");
            const auto where = find_keyword(normalized, "WHERE");
            if (set == std::string::npos || where == std::string::npos ||
                where <= set)
                return std::unexpected("scoped UPDATE requires SET and WHERE");
            const auto assignments = normalized.substr(set + 3,
                where - set - 3);
            if ((partition && assignments.find(uppercase(
                    partition->column_name)) != std::string::npos) ||
                (owner && assignments.find(uppercase(
                    owner->column_name)) != std::string::npos))
                return std::unexpected("data-scope columns are immutable");
        }

        auto qualifier = table_qualifier(statement.sql, table_name);
        auto column = [&qualifier](std::string_view name)
        {
            if (qualifier.empty())
                return std::string{name};
            return std::format("{}.{}", qualifier, name);
        };

        std::string predicate;
        std::vector<param_value> policy_parameters;
        if (partition && !partitions.empty())
        {
            predicate = column(partition->column_name) + " IN (";
            for (std::size_t index = 0; index < partitions.size(); ++index)
            {
                if (index != 0)
                    predicate += ", ";
                predicate += dialect_ == sql_dialect::mysql ? "{}"
                    : std::format("${}", statement.parameters.size() +
                        policy_parameters.size() + 1);
                policy_parameters.push_back(
                    param_value::from_int(partitions[index]));
            }
            predicate += ')';
        }
        if (owner && scope_.include_owner)
        {
            if (!predicate.empty())
                predicate = '(' + predicate + " OR " +
                    column(owner->column_name) + " = " +
                    (dialect_ == sql_dialect::mysql ? "{}"
                        : std::format("${}", statement.parameters.size() +
                            policy_parameters.size() + 1)) + ")";
            else
                predicate = column(owner->column_name) + " = " +
                    (dialect_ == sql_dialect::mysql ? "{}"
                        : std::format("${}", statement.parameters.size() +
                            policy_parameters.size() + 1));
            policy_parameters.push_back(param_value::from_int(scope_.owner_id));
        }
        if (predicate.empty())
            predicate = "1 = 0";

        const auto parameter_position = dialect_ == sql_dialect::mysql
            ? count_placeholders(statement.sql, predicate_position(statement.sql))
            : statement.parameters.size();
        inject_predicate(statement.sql, predicate);
        if (parameter_position > statement.parameters.size())
            return std::unexpected("data permission SQL parameter count mismatch");
        statement.parameters.insert(statement.parameters.begin() +
            parameter_position,
            std::make_move_iterator(policy_parameters.begin()),
            std::make_move_iterator(policy_parameters.end()));
        return statement;
    }

private:
    [[nodiscard]] static auto predicate_position(std::string_view sql)
        -> std::size_t
    {
        const auto normalized = uppercase(sql);
        if (const auto where = find_keyword(normalized, "WHERE");
            where != std::string_view::npos)
            return where + 5;
        return std::min({position_or_end(normalized, "ORDER"),
            position_or_end(normalized, "GROUP"),
            position_or_end(normalized, "HAVING"),
            position_or_end(normalized, "LIMIT")});
    }

    [[nodiscard]] static auto count_placeholders(std::string_view sql,
        std::size_t end) -> std::size_t
    {
        std::size_t count{};
        char quote{};
        for (std::size_t index = 0; index + 1 < end; ++index)
        {
            if (quote != 0)
            {
                if (sql[index] == quote)
                    quote = 0;
                continue;
            }
            if (sql[index] == '\'' || sql[index] == '"' || sql[index] == '`')
            {
                quote = sql[index];
                continue;
            }
            if (sql[index] == '{' && sql[index + 1] == '}')
            {
                ++count;
                ++index;
            }
        }
        return count;
    }

    [[nodiscard]] static auto table_qualifier(std::string_view sql,
        std::string_view table) -> std::string
    {
        auto position = sql.find(table);
        if (position == std::string_view::npos)
            return {};
        position += table.size();
        while (position < sql.size() &&
            (sql[position] == '`' || sql[position] == '"' || std::isspace(
                static_cast<unsigned char>(sql[position]))))
            ++position;
        const auto begin = position;
        while (position < sql.size() &&
            (std::isalnum(static_cast<unsigned char>(sql[position])) ||
                sql[position] == '_'))
            ++position;
        auto token = sql.substr(begin, position - begin);
        auto keyword = uppercase(token);
        if (keyword == "AS")
        {
            while (position < sql.size() && std::isspace(
                static_cast<unsigned char>(sql[position])))
                ++position;
            const auto alias_begin = position;
            while (position < sql.size() &&
                (std::isalnum(static_cast<unsigned char>(sql[position])) ||
                    sql[position] == '_'))
                ++position;
            token = sql.substr(alias_begin, position - alias_begin);
            keyword = uppercase(token);
        }
        if (token.empty() || keyword == "WHERE" || keyword == "SET" ||
            keyword == "JOIN" || keyword == "ORDER" || keyword == "GROUP" ||
            keyword == "LIMIT")
            return {};
        return std::string{token};
    }

    static void inject_predicate(std::string& sql, std::string_view predicate)
    {
        const auto normalized = uppercase(sql);
        if (const auto where = find_keyword(normalized, "WHERE");
            where != std::string::npos)
        {
            const auto tail = std::min({position_or_end(normalized, "ORDER"),
                position_or_end(normalized, "GROUP"),
                position_or_end(normalized, "HAVING"),
                position_or_end(normalized, "LIMIT"),
                position_or_end(normalized, "RETURNING")});
            const auto original = sql.substr(where + 5, tail - where - 5);
            sql.replace(where + 5, tail - where - 5,
                std::format("({}) AND ({}) ", predicate, original));
            return;
        }
        const auto tail = std::min({position_or_end(normalized, "ORDER"),
            position_or_end(normalized, "GROUP"),
            position_or_end(normalized, "HAVING"),
            position_or_end(normalized, "LIMIT")});
        sql.insert(tail, std::format(" WHERE ({}) ", predicate));
    }

    [[nodiscard]] static auto position_or_end(
        std::string_view sql, std::string_view token) noexcept -> std::size_t
    {
        const auto position = find_keyword(sql, token);
        return position == std::string_view::npos ? sql.size() : position;
    }

    [[nodiscard]] static auto find_keyword(std::string_view sql,
        std::string_view keyword) noexcept -> std::size_t
    {
        char quote{};
        for (std::size_t index = 0; index + keyword.size() <= sql.size(); ++index)
        {
            const auto character = sql[index];
            if (quote != 0)
            {
                if (character == quote)
                    quote = 0;
                continue;
            }
            if (character == '\'' || character == '"' || character == '`')
            {
                quote = character;
                continue;
            }
            if (sql.substr(index, keyword.size()) != keyword)
                continue;
            const auto word_character = [](unsigned char value)
            {
                return std::isalnum(value) || value == '_';
            };
            if (index != 0 && word_character(
                static_cast<unsigned char>(sql[index - 1])))
                continue;
            if (index + keyword.size() < sql.size() && word_character(
                static_cast<unsigned char>(sql[index + keyword.size()])))
                continue;
            return index;
        }
        return std::string_view::npos;
    }

    [[nodiscard]] static auto uppercase(std::string_view value) -> std::string
    {
        std::string result{value};
        std::ranges::transform(result, result.begin(), [](unsigned char ch)
            { return static_cast<char>(std::toupper(ch)); });
        return result;
    }

    data_permission_scope scope_;
    sql_dialect dialect_;
    bool enforce_insert_;
    std::string mapped_table_;
};

} // namespace cnetmod::orm
