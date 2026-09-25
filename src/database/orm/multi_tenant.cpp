module cnetmod.orm.multi_tenant;

namespace cnetmod::orm {

namespace {

struct sql_token
{
    std::string word;
    std::size_t begin{};
    std::size_t end{};
};

auto uppercase(std::string_view value) -> std::string
{
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](unsigned char character)
        { return static_cast<char>(std::toupper(character)); });
    return result;
}

auto tokens_of(std::string_view sql)
    -> std::expected<std::vector<sql_token>, std::string>
{
    std::vector<sql_token> result;
    for (std::size_t index = 0; index < sql.size();)
    {
        if (std::isspace(static_cast<unsigned char>(sql[index])))
        {
            ++index;
            continue;
        }
        if (sql[index] == ';' || sql[index] == '#' ||
            (sql[index] == '-' && index + 1 < sql.size() && sql[index + 1] == '-') ||
            (sql[index] == '/' && index + 1 < sql.size() && sql[index + 1] == '*'))
            return std::unexpected("tenant-scoped SQL forbids comments and multiple statements");
        const auto begin = index;
        if (sql[index] == '\'' || sql[index] == '`' || sql[index] == '"')
        {
            const auto quote = sql[index++];
            bool closed = false;
            while (index < sql.size())
            {
                if (sql[index] == '\\' && quote == '\'' && index + 1 < sql.size())
                {
                    index += 2;
                    continue;
                }
                if (sql[index++] != quote)
                    continue;
                if (index < sql.size() && sql[index] == quote)
                {
                    ++index;
                    continue;
                }
                closed = true;
                break;
            }
            if (!closed)
                return std::unexpected("unterminated SQL quoted value");
            if (quote != '\'')
                result.push_back({uppercase(sql.substr(begin + 1, index - begin - 2)),
                    begin, index});
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(sql[index])) || sql[index] == '_')
        {
            while (index < sql.size() &&
                (std::isalnum(static_cast<unsigned char>(sql[index])) ||
                    sql[index] == '_'))
                ++index;
            result.push_back({uppercase(sql.substr(begin, index - begin)), begin, index});
            continue;
        }
        if (sql[index] == '$')
        {
            if (index + 1 >= sql.size() ||
                !std::isdigit(static_cast<unsigned char>(sql[index + 1])))
                return std::unexpected("unsupported SQL dollar quoting in tenant scope");
            ++index;
            while (index < sql.size() &&
                std::isdigit(static_cast<unsigned char>(sql[index])))
                ++index;
            result.push_back({std::string{sql.substr(begin, index - begin)}, begin, index});
            continue;
        }
        ++index;
        result.push_back({std::string{sql.substr(begin, 1)}, begin, index});
    }
    return result;
}

auto find_word(const std::vector<sql_token>& tokens, std::string_view word,
    std::size_t start = 0) -> std::size_t
{
    for (auto index = start; index < tokens.size(); ++index)
        if (tokens[index].word == word)
            return index;
    return tokens.size();
}

auto allowed_id(std::int64_t id, const std::vector<std::int64_t>& allowed)
    -> bool
{
    return id > 0 && std::ranges::find(allowed, id) != allowed.end();
}

auto numeric_id(const param_value& value) -> std::optional<std::int64_t>
{
    if (value.kind == param_value::kind_t::int64_kind)
        return value.int_val;
    if (value.kind == param_value::kind_t::uint64_kind &&
        value.uint_val <= static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()))
        return static_cast<std::int64_t>(value.uint_val);
    return std::nullopt;
}

auto placeholder_count(std::string_view sql, std::size_t end) -> std::size_t
{
    std::size_t count{};
    char quote{};
    for (std::size_t index = 0; index + 1 < end; ++index)
    {
        if (quote != 0)
        {
            if (sql[index] == '\\' && quote == '\'' && index + 1 < end)
            {
                ++index;
                continue;
            }
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

auto placeholder_index(std::string_view expression, std::string_view sql,
    std::size_t expression_begin, sql_dialect dialect)
    -> std::optional<std::size_t>
{
    while (!expression.empty() && std::isspace(
        static_cast<unsigned char>(expression.front())))
    {
        expression.remove_prefix(1);
        ++expression_begin;
    }
    while (!expression.empty() && std::isspace(
        static_cast<unsigned char>(expression.back())))
        expression.remove_suffix(1);
    if (dialect == sql_dialect::mysql)
        return expression == "{}"
            ? std::optional{placeholder_count(sql, expression_begin)}
            : std::nullopt;
    if (expression.size() < 2 || expression.front() != '$')
        return std::nullopt;
    std::size_t number{};
    const auto [end, error] = std::from_chars(expression.data() + 1,
        expression.data() + expression.size(), number);
    if (error != std::errc{} || end != expression.data() + expression.size() ||
        number == 0)
        return std::nullopt;
    return number - 1;
}

auto insert_scoped(intercepted_statement statement, std::string_view table,
    std::string_view column, const tenant_scope& scope, sql_dialect dialect)
    -> std::expected<intercepted_statement, std::string>
{
    auto parsed = tokens_of(statement.sql);
    if (!parsed)
        return std::unexpected(parsed.error());
    const auto& words = *parsed;
    if (words.size() < 8 || words.front().word != "INSERT" ||
        words[1].word != "INTO" || words[2].word != uppercase(table) ||
        words[3].word != "(")
        return std::unexpected("tenant-scoped INSERT requires a mapped column list");
    if (find_word(words, "ON") != words.size() ||
        find_word(words, "SELECT") != words.size())
        return std::unexpected("tenant-scoped upsert/INSERT SELECT is unsupported");

    auto columns_end = find_word(words, ")", 4);
    if (columns_end == words.size() || columns_end + 2 >= words.size() ||
        words[columns_end + 1].word != "VALUES" ||
        words[columns_end + 2].word != "(")
        return std::unexpected("tenant-scoped INSERT has unsupported syntax");
    auto values_end = find_word(words, ")", columns_end + 3);
    if (values_end == words.size() ||
        (values_end + 1 < words.size() && words[values_end + 1].word != "RETURNING"))
        return std::unexpected("tenant-scoped INSERT requires one VALUES row");

    std::vector<std::string> columns;
    for (auto index = std::size_t{4}; index < columns_end; ++index)
    {
        if (index % 2 == 0)
            columns.push_back(words[index].word);
        else if (words[index].word != ",")
            return std::unexpected("tenant-scoped INSERT has invalid columns");
    }
    std::vector<std::pair<std::size_t, std::size_t>> values;
    auto value_begin = words[columns_end + 2].end;
    auto depth = 0;
    for (auto index = columns_end + 3; index <= values_end; ++index)
    {
        if (index == values_end || (words[index].word == "," && depth == 0))
        {
            values.emplace_back(value_begin, words[index].begin);
            value_begin = words[index].end;
        }
        else if (words[index].word == "(")
            ++depth;
        else if (words[index].word == ")")
            --depth;
    }
    if (columns.size() != values.size())
        return std::unexpected("tenant-scoped INSERT column/value mismatch");

    const auto found = std::ranges::find(columns, uppercase(column));
    if (found != columns.end())
    {
        const auto position = static_cast<std::size_t>(found - columns.begin());
        const auto [begin, end] = values[position];
        const auto parameter = placeholder_index(
            std::string_view{statement.sql}.substr(begin, end - begin),
            statement.sql, begin, dialect);
        if (!parameter || *parameter >= statement.parameters.size())
            return std::unexpected("tenant INSERT value must be a bound parameter");
        const auto id = numeric_id(statement.parameters[*parameter]);
        if (!id || !allowed_id(*id, scope.writable_tenant_ids))
            return std::unexpected("tenant INSERT target is not writable");
        return statement;
    }

    if (!allowed_id(scope.tenant_id, scope.writable_tenant_ids))
        return std::unexpected("default tenant INSERT target is not writable");
    const auto value_end_position = words[values_end].begin;
    const auto column_end_position = words[columns_end].begin;
    const auto parameter_position = placeholder_count(statement.sql, value_end_position);
    auto marker = dialect == sql_dialect::mysql ? std::string{"{}"}
        : std::format("${}", statement.parameters.size() + 1);
    statement.sql.insert(value_end_position, ", " + marker);
    statement.sql.insert(column_end_position, ", " + std::string{column});
    if (dialect == sql_dialect::mysql)
        statement.parameters.insert(statement.parameters.begin() +
            std::min(parameter_position, statement.parameters.size()),
            param_value::from_int(scope.tenant_id));
    else
        statement.parameters.push_back(param_value::from_int(scope.tenant_id));
    return statement;
}

} // namespace

auto validate_bound_insert_column(intercepted_statement statement,
    std::string_view table, std::string_view column,
    const std::vector<std::int64_t>& allowed_ids, sql_dialect dialect)
    -> std::expected<intercepted_statement, std::string>
{
    tenant_scope validation;
    validation.writable_tenant_ids = allowed_ids;
    return insert_scoped(std::move(statement), table, column, validation,
        dialect);
}

auto apply_tenant_scope(sql_operation operation,
    intercepted_statement statement, std::string_view table,
    std::string_view tenant_column, const tenant_scope& scope,
    sql_dialect dialect)
    -> std::expected<intercepted_statement, std::string>
{
    if (operation == sql_operation::insert)
        return insert_scoped(std::move(statement), table, tenant_column,
            scope, dialect);
    if (operation == sql_operation::execute)
    {
        auto parsed = tokens_of(statement.sql);
        if (!parsed)
            return std::unexpected(parsed.error());
        if (!parsed->empty())
        {
            const auto& words = *parsed;
            const auto& command = words.front().word;
            const auto simple = words.size() == 1 ||
                (words.size() == 2 &&
                    (words[1].word == "WORK" ||
                        words[1].word == "TRANSACTION"));
            if ((command == "BEGIN" && simple) ||
                (command == "COMMIT" && simple) ||
                (command == "ROLLBACK" && simple) ||
                (command == "START" && words.size() == 2 &&
                    words[1].word == "TRANSACTION") ||
                (command == "SAVEPOINT" && words.size() == 2) ||
                (command == "RELEASE" && words.size() == 3 &&
                    words[1].word == "SAVEPOINT") ||
                (command == "ROLLBACK" && words.size() == 4 &&
                    words[1].word == "TO" && words[2].word == "SAVEPOINT"))
                return statement;
            if (command == "BEGIN" && words.size() >= 4 &&
                words[1].word == "ISOLATION" && words[2].word == "LEVEL" &&
                ((words.size() == 4 && words[3].word == "SERIALIZABLE") ||
                    (words.size() == 5 && words[3].word == "READ" &&
                        (words[4].word == "COMMITTED" ||
                            words[4].word == "UNCOMMITTED")) ||
                    (words.size() == 5 && words[3].word == "REPEATABLE" &&
                        words[4].word == "READ")))
                return statement;
        }
        return std::unexpected("unsupported SQL in strict tenant scope");
    }

    auto parsed = tokens_of(statement.sql);
    if (!parsed)
        return std::unexpected(parsed.error());
    const auto& words = *parsed;
    if (words.empty())
        return std::unexpected("empty SQL in strict tenant scope");
    const auto main = operation == sql_operation::query ? "SELECT"
        : operation == sql_operation::update ? "UPDATE" : "DELETE";
    if (words.front().word != main ||
        find_word(words, "JOIN") != words.size() ||
        find_word(words, "UNION") != words.size() ||
        find_word(words, "WITH") != words.size() ||
        find_word(words, "SELECT", 1) != words.size())
        return std::unexpected("tenant-scoped SQL must target one mapped table");
    const auto table_position = operation == sql_operation::update ? 1
        : find_word(words, "FROM") + 1;
    if (table_position >= words.size() ||
        words[table_position].word != uppercase(table))
        return std::unexpected("tenant-scoped SQL targets an unmapped table");
    auto after_table = table_position + 1;
    if (after_table < words.size() && words[after_table].word == "AS")
        ++after_table;
    if (after_table < words.size() &&
        words[after_table].word != "WHERE" &&
        words[after_table].word != "SET" &&
        words[after_table].word != "GROUP" &&
        words[after_table].word != "ORDER" &&
        words[after_table].word != "LIMIT" &&
        words[after_table].word != "HAVING")
        ++after_table; // Optional single-table alias.
    if (after_table < words.size() &&
        words[after_table].word != "WHERE" &&
        words[after_table].word != "SET" &&
        words[after_table].word != "GROUP" &&
        words[after_table].word != "ORDER" &&
        words[after_table].word != "LIMIT" &&
        words[after_table].word != "HAVING")
        return std::unexpected("tenant-scoped SQL has more than one table");
    if (operation == sql_operation::remove &&
        find_word(words, "WHERE") == words.size())
        return std::unexpected("tenant-scoped DELETE requires WHERE");
    if (operation == sql_operation::update)
    {
        const auto set_position = find_word(words, "SET");
        const auto where_position = find_word(words, "WHERE");
        if (set_position == words.size() || where_position == words.size())
            return std::unexpected("tenant-scoped UPDATE requires WHERE");
        for (auto index = set_position + 1; index < where_position; ++index)
            if (words[index].word == uppercase(tenant_column))
                return std::unexpected("tenant column is immutable");
    }
    const auto& allowed = operation == sql_operation::query
        ? scope.readable_tenant_ids : scope.writable_tenant_ids;
    std::string predicate;
    std::vector<param_value> policy_parameters;
    if (allowed.empty())
        predicate = "1 = 0";
    else
    {
        predicate = std::string{tenant_column} + " IN (";
        for (const auto id : allowed)
        {
            if (id <= 0)
                return std::unexpected("tenant scope contains an invalid ID");
            if (!policy_parameters.empty())
                predicate += ", ";
            predicate += dialect == sql_dialect::mysql ? "{}"
                : std::format("${}", statement.parameters.size() +
                    policy_parameters.size() + 1);
            policy_parameters.push_back(param_value::from_int(id));
        }
        predicate += ')';
    }
    const auto where = find_word(words, "WHERE");
    auto tail = statement.sql.size();
    for (const auto word : {"GROUP", "ORDER", "HAVING", "LIMIT", "RETURNING", "FOR"})
    {
        const auto position = find_word(words, word);
        if (position < words.size() &&
            (where == words.size() || position > where))
            tail = std::min(tail, words[position].begin);
    }
    const auto parameter_position = placeholder_count(statement.sql, tail);
    if (where == words.size())
        statement.sql.insert(tail, " WHERE (" + predicate + ") ");
    else
    {
        auto original = statement.sql.substr(words[where].end,
            tail - words[where].end);
        if (std::ranges::all_of(original, [](unsigned char character)
            { return std::isspace(character); }))
            return std::unexpected("empty WHERE in tenant-scoped SQL");
        statement.sql.replace(words[where].end, tail - words[where].end,
            " (" + original + ") AND (" + predicate + ") ");
    }
    if (dialect == sql_dialect::mysql)
    {
        if (parameter_position > statement.parameters.size())
            return std::unexpected("SQL parameter count mismatch");
        statement.parameters.insert(statement.parameters.begin() +
            parameter_position, policy_parameters.begin(),
            policy_parameters.end());
    }
    else
        statement.parameters.insert(statement.parameters.end(),
            policy_parameters.begin(), policy_parameters.end());
    return statement;
}

thread_local std::optional<std::int64_t> tenant_context::current_tenant_id_;

void tenant_context::set_tenant_id(std::int64_t tenant_id)
{
    current_tenant_id_ = tenant_id;
}

auto tenant_context::get_tenant_id() -> std::optional<std::int64_t>
{
    return current_tenant_id_;
}

void tenant_context::clear()
{
    current_tenant_id_ = std::nullopt;
}

multi_tenant_interceptor::multi_tenant_interceptor(std::string tenant_field)
    : tenant_field_(std::move(tenant_field)) {}

auto multi_tenant_interceptor::inject_tenant_condition_impl(
    std::string sql, std::vector<param_value>& params,
    std::string_view tenant_field, std::int64_t tenant_id) -> std::string
{
    if (const auto where_pos = sql.find(" WHERE ");
        where_pos != std::string::npos)
    {
        sql.insert(where_pos + 7, std::format("`{}` = {{}} AND ", tenant_field));
        params.insert(params.begin(), param_value::from_int(tenant_id));
        return sql;
    }

    const auto order_pos = sql.find(" ORDER BY");
    const auto group_pos = sql.find(" GROUP BY");
    const auto limit_pos = sql.find(" LIMIT");
    const auto having_pos = sql.find(" HAVING");
    const auto insert_pos =
        std::min({order_pos != std::string::npos ? order_pos : sql.size(),
            group_pos != std::string::npos ? group_pos : sql.size(),
            limit_pos != std::string::npos ? limit_pos : sql.size(),
            having_pos != std::string::npos ? having_pos : sql.size()});
    sql.insert(insert_pos, std::format(" WHERE `{}` = {{}}", tenant_field));
    params.push_back(param_value::from_int(tenant_id));
    return sql;
}

auto multi_tenant_interceptor::inject_tenant_insert_impl(
    std::string sql, std::vector<param_value>& params,
    std::string_view tenant_field, std::int64_t tenant_id) -> std::string
{
    const auto values_pos = sql.find(" VALUES ");
    if (values_pos == std::string::npos)
        return sql;
    const auto col_start = sql.find('(');
    const auto col_end = sql.find(')', col_start);
    if (col_start == std::string::npos || col_end == std::string::npos)
        return sql;
    sql.insert(col_end, std::format(", `{}`", tenant_field));
    const auto val_start = sql.find('(', values_pos);
    const auto val_end = sql.find(')', val_start);
    if (val_start != std::string::npos && val_end != std::string::npos)
    {
        sql.insert(val_end, ", {}");
        params.push_back(param_value::from_int(tenant_id));
    }
    return sql;
}

void multi_tenant_interceptor::set_tenant_field(std::string field)
{
    tenant_field_ = std::move(field);
}

auto multi_tenant_interceptor::tenant_field() const noexcept
    -> const std::string&
{
    return tenant_field_;
}

auto global_multi_tenant_interceptor() -> multi_tenant_interceptor&
{
    static multi_tenant_interceptor instance;
    return instance;
}

tenant_guard::tenant_guard(std::int64_t tenant_id)
{
    tenant_context::set_tenant_id(tenant_id);
}

tenant_guard::~tenant_guard()
{
    tenant_context::clear();
}

} // namespace cnetmod::orm
