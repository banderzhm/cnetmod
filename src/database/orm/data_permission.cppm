export module cnetmod.orm.data_permission;

import std;
import cnetmod.orm.interceptor_chain;
import cnetmod.orm.model_metadata;
import cnetmod.orm.sql_parameters;

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
};

/**
 * @brief Injects model-declared row visibility into parameterized SQL.
 */
template <Model T>
class data_permission_interceptor
{
public:
    explicit data_permission_interceptor(data_permission_scope scope)
        : scope_(std::move(scope))
    {
    }

    [[nodiscard]] auto apply(sql_operation operation,
        intercepted_statement statement) const
        -> std::expected<intercepted_statement, std::string>
    {
        if (scope_.unrestricted || operation == sql_operation::insert ||
            operation == sql_operation::execute)
            return statement;

        const auto& metadata = model_traits<T>::meta();
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

        auto qualifier = table_qualifier(statement.sql, metadata.table_name);
        auto column = [&qualifier](std::string_view name)
        {
            if (qualifier.empty())
                return std::string{name};
            return std::format("{}.{}", qualifier, name);
        };

        std::string predicate;
        std::vector<param_value> policy_parameters;
        if (partition && !scope_.partition_ids.empty())
        {
            predicate = column(partition->column_name) + " IN (";
            for (std::size_t index = 0; index < scope_.partition_ids.size(); ++index)
            {
                if (index != 0)
                    predicate += ", ";
                predicate += "{}";
                policy_parameters.push_back(
                    param_value::from_int(scope_.partition_ids[index]));
            }
            predicate += ')';
        }
        if (owner && scope_.include_owner)
        {
            if (!predicate.empty())
                predicate = '(' + predicate + " OR " +
                    column(owner->column_name) + " = {})";
            else
                predicate = column(owner->column_name) + " = {}";
            policy_parameters.push_back(param_value::from_int(scope_.owner_id));
        }
        if (predicate.empty())
            predicate = "1 = 0";

        inject_predicate(statement.sql, predicate);
        statement.parameters.insert(statement.parameters.begin(),
            std::make_move_iterator(policy_parameters.begin()),
            std::make_move_iterator(policy_parameters.end()));
        return statement;
    }

private:
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
        if (const auto where = normalized.find(" WHERE ");
            where != std::string::npos)
        {
            sql.insert(where + 7, std::format("({}) AND ", predicate));
            return;
        }
        const auto tail = std::min({position_or_end(normalized, " ORDER BY"),
            position_or_end(normalized, " GROUP BY"),
            position_or_end(normalized, " HAVING"),
            position_or_end(normalized, " LIMIT")});
        sql.insert(tail, std::format(" WHERE ({})", predicate));
    }

    [[nodiscard]] static auto position_or_end(
        std::string_view sql, std::string_view token) noexcept -> std::size_t
    {
        const auto position = sql.find(token);
        return position == std::string_view::npos ? sql.size() : position;
    }

    [[nodiscard]] static auto uppercase(std::string_view value) -> std::string
    {
        std::string result{value};
        std::ranges::transform(result, result.begin(), [](unsigned char ch)
            { return static_cast<char>(std::toupper(ch)); });
        return result;
    }

    data_permission_scope scope_;
};

} // namespace cnetmod::orm
