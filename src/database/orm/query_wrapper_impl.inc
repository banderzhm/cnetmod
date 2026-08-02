namespace cnetmod::orm {

// =============================================================================
// Exported helpers used by the query-wrapper template definitions below.
// =============================================================================

export namespace query_wrapper_detail {

inline auto join_type_str(join_type type) -> std::string_view
{
    switch (type)
    {
    case join_type::inner:      return "INNER JOIN";
    case join_type::left:       return "LEFT JOIN";
    case join_type::right:      return "RIGHT JOIN";
    case join_type::full_outer: return "FULL OUTER JOIN";
    case join_type::cross:      return "CROSS JOIN";
    }
    return "JOIN";
}

auto aggregate_func_str(aggregate_func func) -> std::string_view
{
    switch (func)
    {
    case aggregate_func::count:          return "COUNT";
    case aggregate_func::sum:            return "SUM";
    case aggregate_func::avg:            return "AVG";
    case aggregate_func::min:            return "MIN";
    case aggregate_func::max:            return "MAX";
    case aggregate_func::count_distinct: return "COUNT";
    }
    return "COUNT";
}

/// Build an aggregate SQL fragment, e.g.  COUNT(*)  or  COUNT(DISTINCT col)
inline auto build_aggregate_fragment(const aggregate_column& agg,
                                     const dialect_config& cfg) -> std::string
{
    auto func_name = aggregate_func_str(agg.func);
    std::string sql;
    if (agg.func == aggregate_func::count_distinct)
    {
        sql = std::format("COUNT(DISTINCT {})",
                          agg.column == "*" ? "*" : quote_identifier(agg.column, cfg));
    }
    else
    {
        sql = std::format("{}({})",
                          func_name,
                          agg.column == "*" ? "*" : quote_identifier(agg.column, cfg));
    }
    if (!agg.alias.empty())
    {
        sql += std::format(" AS {}", quote_identifier(agg.alias, cfg));
    }
    return sql;
}

/// Build a HAVING aggregate fragment (without alias)
inline auto build_having_aggregate_fragment(aggregate_func func,
                                            const std::string& column,
                                            const dialect_config& cfg) -> std::string
{
    auto func_name = aggregate_func_str(func);
    if (func == aggregate_func::count_distinct)
    {
        return std::format("COUNT(DISTINCT {})",
                           column == "*" ? "*" : quote_identifier(column, cfg));
    }
    return std::format("{}({})",
                       func_name,
                       column == "*" ? "*" : quote_identifier(column, cfg));
}

} // namespace query_wrapper_detail

// =============================================================================
// query_wrapper<T> — non-template method implementations
// =============================================================================

// -- LIKE / NULL / boolean --------------------------------------------------

template <Model T>
auto query_wrapper<T>::like(std::string_view column, std::string_view pattern)
    -> query_wrapper&
{
    add_condition(column, compare_op::like, param_value::from_string(std::string(pattern)));
    return *this;
}

template <Model T>
auto query_wrapper<T>::not_like(std::string_view column, std::string_view pattern)
    -> query_wrapper&
{
    add_condition(column, compare_op::not_like, param_value::from_string(std::string(pattern)));
    return *this;
}

template <Model T>
auto query_wrapper<T>::is_null(std::string_view column) -> query_wrapper&
{
    add_condition(column, compare_op::is_null, {});
    return *this;
}

template <Model T>
auto query_wrapper<T>::is_not_null(std::string_view column) -> query_wrapper&
{
    add_condition(column, compare_op::is_not_null, {});
    return *this;
}

template <Model T>
auto query_wrapper<T>::is_true(std::string_view column) -> query_wrapper&
{
    add_condition(column, compare_op::is_true, {});
    return *this;
}

template <Model T>
auto query_wrapper<T>::is_false(std::string_view column) -> query_wrapper&
{
    add_condition(column, compare_op::is_false, {});
    return *this;
}

// -- Raw --------------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::raw(std::string_view raw_sql) -> query_wrapper&
{
    add_condition(raw_sql, compare_op::raw, {});
    return *this;
}

// -- LIKE helpers -----------------------------------------------------------

template <Model T>
auto query_wrapper<T>::starts_with(std::string_view column, std::string_view prefix)
    -> query_wrapper&
{
    return like(column, std::format("{}%", prefix));
}

template <Model T>
auto query_wrapper<T>::ends_with(std::string_view column, std::string_view suffix)
    -> query_wrapper&
{
    return like(column, std::format("%{}", suffix));
}

template <Model T>
auto query_wrapper<T>::contains(std::string_view column, std::string_view substring)
    -> query_wrapper&
{
    return like(column, std::format("%{}%", substring));
}

// -- Logical operators ------------------------------------------------------

template <Model T>
auto query_wrapper<T>::and_() -> query_wrapper&
{
    current_logic_ = logic_op::and_op;
    return *this;
}

template <Model T>
auto query_wrapper<T>::or_() -> query_wrapper&
{
    current_logic_ = logic_op::or_op;
    return *this;
}

template <Model T>
auto query_wrapper<T>::and_(const query_wrapper& nested) -> query_wrapper&
{
    condition cond;
    cond.is_group = true;
    cond.children = nested.conditions_;
    cond.connector = logic_op::and_op;
    conditions_.push_back(std::move(cond));
    return *this;
}

template <Model T>
auto query_wrapper<T>::or_(const query_wrapper& nested) -> query_wrapper&
{
    condition cond;
    cond.is_group = true;
    cond.children = nested.conditions_;
    cond.connector = logic_op::or_op;
    conditions_.push_back(std::move(cond));
    return *this;
}

// -- ORDER BY ---------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::order_by_asc(std::string_view column) -> query_wrapper&
{
    order_by_.push_back({std::string(column), order_dir::asc});
    return *this;
}

template <Model T>
auto query_wrapper<T>::order_by_desc(std::string_view column) -> query_wrapper&
{
    order_by_.push_back({std::string(column), order_dir::desc});
    return *this;
}

// -- LIMIT / OFFSET ---------------------------------------------------------

template <Model T>
auto query_wrapper<T>::limit(std::int64_t count) -> query_wrapper&
{
    limit_ = count;
    return *this;
}

template <Model T>
auto query_wrapper<T>::offset(std::int64_t count) -> query_wrapper&
{
    offset_ = count;
    return *this;
}

// -- SELECT columns ---------------------------------------------------------

template <Model T>
auto query_wrapper<T>::select(std::initializer_list<std::string_view> columns)
    -> query_wrapper&
{
    select_columns_.clear();
    for (auto col : columns)
        select_columns_.push_back(std::string(col));
    return *this;
}

// -- GROUP BY / HAVING ------------------------------------------------------

template <Model T>
auto query_wrapper<T>::group_by(std::string_view column) -> query_wrapper&
{
    group_by_.push_back(std::string(column));
    return *this;
}

template <Model T>
auto query_wrapper<T>::group_by(std::initializer_list<std::string_view> columns)
    -> query_wrapper&
{
    for (auto col : columns)
        group_by_.push_back(std::string(col));
    return *this;
}

template <Model T>
auto query_wrapper<T>::having(std::string_view condition) -> query_wrapper&
{
    having_ = condition;
    return *this;
}

// -- JOIN -------------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::join(std::string_view table, std::string_view condition,
                            join_type type) -> query_wrapper&
{
    joins_.push_back({type, std::string(table), std::string(condition)});
    return *this;
}

template <Model T>
auto query_wrapper<T>::inner_join(std::string_view table, std::string_view condition)
    -> query_wrapper&
{
    return join(table, condition, join_type::inner);
}

template <Model T>
auto query_wrapper<T>::left_join(std::string_view table, std::string_view condition)
    -> query_wrapper&
{
    return join(table, condition, join_type::left);
}

template <Model T>
auto query_wrapper<T>::right_join(std::string_view table, std::string_view condition)
    -> query_wrapper&
{
    return join(table, condition, join_type::right);
}

template <Model T>
auto query_wrapper<T>::full_outer_join(std::string_view table, std::string_view condition)
    -> query_wrapper&
{
    return join(table, condition, join_type::full_outer);
}

// -- Aggregate SELECT -------------------------------------------------------

template <Model T>
auto query_wrapper<T>::select_aggregate(aggregate_func func, std::string_view column,
                                        std::string_view alias) -> query_wrapper&
{
    aggregate_columns_.push_back({func, std::string(column), std::string(alias)});
    return *this;
}

template <Model T>
auto query_wrapper<T>::select_count(std::string_view column, std::string_view alias)
    -> query_wrapper&
{
    return select_aggregate(aggregate_func::count, column, alias);
}

template <Model T>
auto query_wrapper<T>::select_sum(std::string_view column, std::string_view alias)
    -> query_wrapper&
{
    return select_aggregate(aggregate_func::sum, column, alias);
}

template <Model T>
auto query_wrapper<T>::select_avg(std::string_view column, std::string_view alias)
    -> query_wrapper&
{
    return select_aggregate(aggregate_func::avg, column, alias);
}

template <Model T>
auto query_wrapper<T>::select_min(std::string_view column, std::string_view alias)
    -> query_wrapper&
{
    return select_aggregate(aggregate_func::min, column, alias);
}

template <Model T>
auto query_wrapper<T>::select_max(std::string_view column, std::string_view alias)
    -> query_wrapper&
{
    return select_aggregate(aggregate_func::max, column, alias);
}

// -- Accessors --------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::conditions() const noexcept -> const std::vector<condition>&
{
    return conditions_;
}

template <Model T>
auto query_wrapper<T>::is_empty() const noexcept -> bool
{
    return conditions_.empty();
}

// ===========================================================================
// Build SQL
// ===========================================================================

// -- SELECT -----------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::build_select_sql() const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_select_sql(get_dialect_config(sql_dialect::mysql));
}

template <Model T>
auto query_wrapper<T>::build_select_sql(sql_dialect dialect) const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_select_sql(get_dialect_config(dialect));
}

template <Model T>
auto query_wrapper<T>::build_select_sql(const dialect_config& cfg) const
    -> std::pair<std::string, std::vector<param_value>>
{
    auto& meta = model_traits<T>::meta();
    std::string sql = "SELECT ";

    // --- SELECT columns (regular + aggregates) ---
    bool has_regular = !select_columns_.empty();
    bool has_agg = !aggregate_columns_.empty();

    if (!has_regular && !has_agg)
    {
        sql += "*";
    }
    else
    {
        bool first = true;
        if (has_regular)
        {
            for (auto& col : select_columns_)
            {
                if (!first) sql += ", ";
                sql += quote_identifier(col, cfg);
                first = false;
            }
        }
        if (has_agg)
        {
            for (auto& agg : aggregate_columns_)
            {
                if (!first) sql += ", ";
                sql += query_wrapper_detail::build_aggregate_fragment(agg, cfg);
                first = false;
            }
        }
    }

    // --- FROM ---
    sql += " FROM " + quote_identifier(meta.table_name, cfg);

    // --- JOIN ---
    for (auto& j : joins_)
    {
        sql += std::format(" {} {} ON {}",
                           query_wrapper_detail::join_type_str(j.type),
                           quote_identifier(j.table, cfg),
                           j.condition);
    }

    // --- WHERE ---
    std::vector<param_value> params;
    int param_index = 0;
    if (!conditions_.empty())
    {
        sql += " WHERE ";
        sql += build_where_clause(conditions_, params, cfg, param_index);
    }

    // --- GROUP BY ---
    if (!group_by_.empty())
    {
        sql += " GROUP BY ";
        for (std::size_t i = 0; i < group_by_.size(); ++i)
        {
            if (i > 0) sql += ", ";
            sql += quote_identifier(group_by_[i], cfg);
        }
    }

    // --- HAVING (raw string + structured aggregate conditions) ---
    bool has_raw_having = !having_.empty();
    bool has_struct_having = !having_conditions_.empty();

    if (has_raw_having || has_struct_having)
    {
        sql += " HAVING ";
        if (has_raw_having)
            sql += having_;

        for (std::size_t i = 0; i < having_conditions_.size(); ++i)
        {
            if (i > 0 || has_raw_having)
            {
                sql += having_conditions_[i].connector == logic_op::and_op
                           ? " AND "
                           : " OR ";
            }
            auto& h = having_conditions_[i];
            sql += query_wrapper_detail::build_having_aggregate_fragment(h.func, h.column, cfg);
            ++param_index;
            sql += std::format(" {} {}", h.op, make_placeholder(param_index, cfg));
            params.push_back(h.value);
        }
    }

    // --- ORDER BY ---
    if (!order_by_.empty())
    {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < order_by_.size(); ++i)
        {
            if (i > 0) sql += ", ";
            sql += quote_identifier(order_by_[i].column, cfg);
            sql += order_by_[i].direction == order_dir::asc ? " ASC" : " DESC";
        }
    }

    // --- LIMIT / OFFSET ---
    if (limit_ > 0)
    {
        sql += std::format(" LIMIT {}", limit_);
        if (offset_ > 0)
            sql += std::format(" OFFSET {}", offset_);
    }

    return {sql, params};
}

// -- COUNT ------------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::build_count_sql() const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_count_sql(get_dialect_config(sql_dialect::mysql));
}

template <Model T>
auto query_wrapper<T>::build_count_sql(sql_dialect dialect) const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_count_sql(get_dialect_config(dialect));
}

template <Model T>
auto query_wrapper<T>::build_count_sql(const dialect_config& cfg) const
    -> std::pair<std::string, std::vector<param_value>>
{
    auto& meta = model_traits<T>::meta();
    std::string sql = "SELECT COUNT(*) FROM " + quote_identifier(meta.table_name, cfg);

    // JOIN
    for (auto& j : joins_)
    {
        sql += std::format(" {} {} ON {}",
                           query_wrapper_detail::join_type_str(j.type),
                           quote_identifier(j.table, cfg),
                           j.condition);
    }

    std::vector<param_value> params;
    int param_index = 0;
    if (!conditions_.empty())
    {
        sql += " WHERE ";
        sql += build_where_clause(conditions_, params, cfg, param_index);
    }

    return {sql, params};
}

// -- DELETE -----------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::build_delete_sql() const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_delete_sql(get_dialect_config(sql_dialect::mysql));
}

template <Model T>
auto query_wrapper<T>::build_delete_sql(sql_dialect dialect) const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_delete_sql(get_dialect_config(dialect));
}

template <Model T>
auto query_wrapper<T>::build_delete_sql(const dialect_config& cfg) const
    -> std::pair<std::string, std::vector<param_value>>
{
    auto& meta = model_traits<T>::meta();
    std::string sql = "DELETE FROM " + quote_identifier(meta.table_name, cfg);

    std::vector<param_value> params;
    int param_index = 0;
    if (!conditions_.empty())
    {
        sql += " WHERE ";
        sql += build_where_clause(conditions_, params, cfg, param_index);
    }

    if (cfg.supports_returning)
        sql += " RETURNING *";

    return {sql, params};
}

// -- UPDATE -----------------------------------------------------------------

template <Model T>
auto query_wrapper<T>::build_update_sql(const T& entity) const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_update_sql(entity, get_dialect_config(sql_dialect::mysql));
}

template <Model T>
auto query_wrapper<T>::build_update_sql(const T& entity, sql_dialect dialect) const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_update_sql(entity, get_dialect_config(dialect));
}

template <Model T>
auto query_wrapper<T>::build_update_sql(const T& entity, const dialect_config& cfg) const
    -> std::pair<std::string, std::vector<param_value>>
{
    auto& meta = model_traits<T>::meta();
    std::string sql = "UPDATE " + quote_identifier(meta.table_name, cfg) + " SET ";

    std::vector<param_value> params;
    int param_index = 0;
    bool first = true;
    for (auto& field : meta.fields)
    {
        if (has_flag(field.col.flags, col_flag::primary_key))
            continue;
        if (has_flag(field.col.flags, col_flag::auto_increment))
            continue;

        if (!first)
            sql += ", ";
        ++param_index;
        sql += quote_identifier(field.col.column_name, cfg) + " = " + make_placeholder(param_index, cfg);
        params.push_back(field.getter(entity));
        first = false;
    }

    if (!conditions_.empty())
    {
        sql += " WHERE ";
        sql += build_where_clause(conditions_, params, cfg, param_index);
    }

    if (cfg.supports_returning)
        sql += " RETURNING *";

    return {sql, params};
}

// -- Stand-alone WHERE clause builder ----------------------------------------

template <Model T>
auto query_wrapper<T>::build_where_sql() const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_where_sql(get_dialect_config(sql_dialect::mysql));
}

template <Model T>
auto query_wrapper<T>::build_where_sql(sql_dialect dialect) const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_where_sql(get_dialect_config(dialect));
}

template <Model T>
auto query_wrapper<T>::build_where_sql(const dialect_config& cfg, int initial_param_index) const
    -> std::pair<std::string, std::vector<param_value>>
{
    std::vector<param_value> params;
    int param_index = initial_param_index;
    std::string sql = build_where_clause(conditions_, params, cfg, param_index);
    return {sql, params};
}

// ===========================================================================
// Private helpers
// ===========================================================================

template <Model T>
void query_wrapper<T>::add_condition(std::string_view column, compare_op op,
                                     std::vector<param_value> values)
{
    condition cond;
    cond.column = column;
    cond.op = op;
    cond.values = std::move(values);
    cond.connector = current_logic_;
    conditions_.push_back(std::move(cond));
    current_logic_ = logic_op::and_op; // Reset to AND
}

template <Model T>
void query_wrapper<T>::add_condition(std::string_view column, compare_op op,
                                     param_value value)
{
    std::vector<param_value> values;
    values.push_back(std::move(value));
    add_condition(column, op, std::move(values));
}

template <Model T>
auto query_wrapper<T>::build_where_clause(
    const std::vector<condition>& conds,
    std::vector<param_value>& params,
    const dialect_config& cfg,
    int& param_index) -> std::string
{
    std::string sql;
    for (std::size_t i = 0; i < conds.size(); ++i)
    {
        auto& cond = conds[i];

        // Logical connector
        if (i > 0)
            sql += cond.connector == logic_op::and_op ? " AND " : " OR ";

        // Nested group
        if (cond.is_group)
        {
            sql += "(";
            sql += build_where_clause(cond.children, params, cfg, param_index);
            sql += ")";
            continue;
        }

        // Raw SQL fragment
        if (cond.op == compare_op::raw)
        {
            sql += cond.column;
            continue;
        }

        // Build condition
        sql += quote_identifier(cond.column, cfg);

        switch (cond.op)
        {
        case compare_op::eq:
            ++param_index;
            sql += " = " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::ne:
            ++param_index;
            sql += " != " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::gt:
            ++param_index;
            sql += " > " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::ge:
            ++param_index;
            sql += " >= " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::lt:
            ++param_index;
            sql += " < " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::le:
            ++param_index;
            sql += " <= " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::like:
            ++param_index;
            sql += " LIKE " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::not_like:
            ++param_index;
            sql += " NOT LIKE " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            break;
        case compare_op::is_null:
            sql += " IS NULL";
            break;
        case compare_op::is_not_null:
            sql += " IS NOT NULL";
            break;
        case compare_op::in:
            sql += " IN (";
            for (std::size_t j = 0; j < cond.values.size(); ++j)
            {
                if (j > 0) sql += ", ";
                ++param_index;
                sql += make_placeholder(param_index, cfg);
                params.push_back(cond.values[j]);
            }
            sql += ")";
            break;
        case compare_op::not_in:
            sql += " NOT IN (";
            for (std::size_t j = 0; j < cond.values.size(); ++j)
            {
                if (j > 0) sql += ", ";
                ++param_index;
                sql += make_placeholder(param_index, cfg);
                params.push_back(cond.values[j]);
            }
            sql += ")";
            break;
        case compare_op::between:
            ++param_index;
            sql += " BETWEEN " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            ++param_index;
            sql += " AND " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[1]);
            break;
        case compare_op::not_between:
            ++param_index;
            sql += " NOT BETWEEN " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[0]);
            ++param_index;
            sql += " AND " + make_placeholder(param_index, cfg);
            params.push_back(cond.values[1]);
            break;
        case compare_op::is_true:
            sql += " = ";
            sql += boolean_literal(true, cfg);
            break;
        case compare_op::is_false:
            sql += " = ";
            sql += boolean_literal(false, cfg);
            break;
        case compare_op::raw:
            break; // Already handled above
        }
    }
    return sql;
}

// =============================================================================
// update_wrapper<T> — non-template method implementations
// =============================================================================

template <Model T>
auto update_wrapper<T>::build_sql() const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_sql(get_dialect_config(sql_dialect::mysql));
}

template <Model T>
auto update_wrapper<T>::build_sql(sql_dialect dialect) const
    -> std::pair<std::string, std::vector<param_value>>
{
    return build_sql(get_dialect_config(dialect));
}

template <Model T>
auto update_wrapper<T>::build_sql(const dialect_config& cfg) const
    -> std::pair<std::string, std::vector<param_value>>
{
    auto& meta = model_traits<T>::meta();
    std::string sql = "UPDATE " + quote_identifier(meta.table_name, cfg) + " SET ";

    std::vector<param_value> params;
    int param_index = 0;
    bool first = true;
    for (auto& [col, val] : set_fields_)
    {
        if (!first)
            sql += ", ";
        ++param_index;
        sql += quote_identifier(col, cfg) + " = " + make_placeholder(param_index, cfg);
        params.push_back(val);
        first = false;
    }

    if (!where_.is_empty())
    {
        auto [where_sql, where_params] = where_.build_where_sql(cfg, param_index);
        sql += " WHERE " + where_sql;
        params.insert(params.end(), where_params.begin(), where_params.end());
    }

    if (cfg.supports_returning)
        sql += " RETURNING *";

    return {sql, params};
}

} // namespace cnetmod::orm
