module cnetmod.orm.semantic_schema_migration;
import std;

namespace cnetmod::orm {
namespace {
    auto identifier(std::string_view value) -> std::string
    {
        if (value.empty() || !std::ranges::all_of(value, [](unsigned char c)
                                 {
                                     return std::isalnum(c) || c == '_';
                                 }))
            throw std::invalid_argument("schema identifier must contain only letters, digits, and underscores");
        return "`" + std::string(value) + "`";
    }

    auto literal(const schema_literal& value) -> std::string
    {
        return std::visit([](const auto& item) -> std::string
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::same_as<T, bool>)
                    return item ? "true" : "false";
                else if constexpr (std::same_as<T, std::int64_t>)
                    return std::to_string(item);
                else
                {
                    std::string result{"'"};
                    for (const char c : item)
                    {
                        if (c == '\'' || c == '\\')
                            result += '\\';
                        result += c;
                    }
                    return result + '\'';
                }
            },
            value);
    }

    auto column_sql(const column_definition& column) -> std::string
    {
        if (column.auto_generated)
            throw std::invalid_argument("ClickHouse does not support relational auto-increment/identity columns");
        std::string type;
        switch (column.type)
        {
        case relational_column_type::boolean:
            type = "Bool";
            break;
        case relational_column_type::int32:
            type = "Int32";
            break;
        case relational_column_type::int64:
            type = "Int64";
            break;
        case relational_column_type::decimal:
            type = "Decimal(38,9)";
            break;
        case relational_column_type::uuid:
            type = "UUID";
            break;
        case relational_column_type::timestamp:
            type = "DateTime64(6,'UTC')";
            break;
        case relational_column_type::varchar:
        case relational_column_type::text:
        case relational_column_type::binary:
            type = "String";
            break;
        case relational_column_type::json:
            throw std::invalid_argument("ClickHouse JSON schema requires an explicit native type mapping");
        }
        if (column.nullable && !column.primary_key)
            type = "Nullable(" + type + ")";
        auto sql = identifier(column.name) + " " + type;
        if (column.default_value)
            sql += " DEFAULT " + std::visit([](const auto& value) -> std::string
                                     {
                                         if constexpr (std::same_as<std::decay_t<decltype(value)>, current_timestamp_default>)
                                             return "now64(6)";
                                         else
                                             return literal(value);
                                     },
                                     *column.default_value);
        return sql;
    }

    auto check_sql(const check_constraint_definition& constraint) -> std::string
    {
        auto expression = std::visit([](const auto& item) -> std::string
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::same_as<T, column_like_predicate>)
                    return identifier(item.column) + " LIKE " + literal(schema_literal{item.pattern});
                else if constexpr (std::same_as<T, column_set_predicate>)
                {
                    if (item.values.empty())
                        throw std::invalid_argument("CHECK IN requires values");
                    std::string result = identifier(item.column) + " IN (";
                    for (std::size_t i{}; i < item.values.size(); ++i)
                    {
                        if (i)
                            result += ',';
                        result += literal(item.values[i]);
                    }
                    return result + ")";
                }
                else
                {
                    constexpr std::array<std::string_view, 6> operators{"=", "!=", "<", "<=", ">", ">="};
                    const auto index = static_cast<std::size_t>(item.comparison);
                    if (index >= operators.size())
                        throw std::invalid_argument("invalid CHECK comparison");
                    if constexpr (std::same_as<T, column_literal_predicate>)
                        return identifier(item.column) + " " + std::string(operators[index]) + " " + literal(item.value);
                    else
                        return identifier(item.left_column) + " " + std::string(operators[index]) + " " + identifier(item.right_column);
                }
            },
            constraint.predicate);
        return "CONSTRAINT " + identifier(constraint.name) + " CHECK (" + expression + ")";
    }
} // namespace

auto render_clickhouse_schema(const schema_migration& migration) -> std::expected<std::vector<std::string>, schema_render_error>
{
    try
    {
        std::vector<std::string> statements;
        for (const auto& operation : migration.operations)
        {
            auto sql = std::visit([](const auto& item) -> std::string
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::same_as<T, create_table_operation>)
                    {
                        if (item.table.columns.empty())
                            throw std::invalid_argument("table requires columns");
                        if (!item.table.foreign_keys.empty() || !item.table.unique_constraints.empty() || !item.table.indexes.empty())
                            throw std::invalid_argument("ClickHouse does not provide relational foreign keys, unique constraints, or B-tree indexes");
                        std::string sql = "CREATE TABLE " + identifier(item.table.name) + " (", keys;
                        for (std::size_t i{}; i < item.table.columns.size(); ++i)
                        {
                            if (i)
                                sql += ',';
                            sql += column_sql(item.table.columns[i]);
                            if (item.table.columns[i].primary_key)
                            {
                                if (!keys.empty())
                                    keys += ',';
                                keys += identifier(item.table.columns[i].name);
                            }
                        }
                        for (const auto& constraint : item.table.checks)
                            sql += ',' + check_sql(constraint);
                        return sql + ") ENGINE=MergeTree ORDER BY " + (keys.empty() ? "tuple()" : "(" + keys + ")");
                    }
                    else if constexpr (std::same_as<T, add_column_operation>)
                        return "ALTER TABLE " + identifier(item.table) + " ADD COLUMN " + column_sql(item.column);
                    else if constexpr (std::same_as<T, add_check_constraint_operation>)
                        return "ALTER TABLE " + identifier(item.table) + " ADD " + check_sql(item.constraint);
                    else
                        throw std::invalid_argument("ClickHouse schema operation requires a native index/constraint definition");
                },
                operation);
            statements.push_back(std::move(sql));
        }
        return statements;
    }
    catch (const std::exception& failure)
    {
        return std::unexpected(schema_render_error{failure.what()});
    }
}
} // namespace cnetmod::orm
