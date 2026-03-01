export module cnetmod.protocol.mysql:orm_wrapper;

import std;
import :types;
import :orm_meta;
import :orm_reflect;

namespace cnetmod::mysql::orm {

// =============================================================================
// Helper to convert values to param_value
// =============================================================================

namespace detail {
    inline auto to_param_value(std::int64_t v) -> param_value { return param_value::from_int(v); }
    inline auto to_param_value(std::uint64_t v) -> param_value { return param_value::from_uint(v); }
    inline auto to_param_value(int v) -> param_value { return param_value::from_int(v); }
    inline auto to_param_value(std::uint32_t v) -> param_value { return param_value::from_uint(v); }
    inline auto to_param_value(double v) -> param_value { return param_value::from_double(v); }
    inline auto to_param_value(float v) -> param_value { return param_value::from_double(v); }
    inline auto to_param_value(const std::string& v) -> param_value { return param_value::from_string(v); }
    inline auto to_param_value(std::string_view v) -> param_value { return param_value::from_string(std::string(v)); }
    inline auto to_param_value(const char* v) -> param_value { return param_value::from_string(std::string(v)); }
    inline auto to_param_value(bool v) -> param_value { return param_value::from_int(v ? 1 : 0); }
    inline auto to_param_value(const mysql_date& v) -> param_value { return param_value::from_date(v); }
    inline auto to_param_value(const mysql_datetime& v) -> param_value { return param_value::from_datetime(v); }
    inline auto to_param_value(const mysql_time& v) -> param_value { return param_value::from_time(v); }
}

// =============================================================================
// Comparison operators
// =============================================================================

export enum class compare_op {
    eq,      // =
    ne,      // !=
    gt,      // >
    ge,      // >=
    lt,      // <
    le,      // <=
    like,    // LIKE
    not_like,// NOT LIKE
    in,      // IN
    not_in,  // NOT IN
    is_null, // IS NULL
    is_not_null, // IS NOT NULL
    between, // BETWEEN
};

// =============================================================================
// Logical operators
// =============================================================================

export enum class logic_op {
    and_op,
    or_op,
};

// =============================================================================
// Order direction
// =============================================================================

export enum class order_dir {
    asc,
    desc,
};

// =============================================================================
// Condition node
// =============================================================================

export struct condition {
    std::string column;
    compare_op op = compare_op::eq;
    std::vector<param_value> values;
    logic_op connector = logic_op::and_op;

    // For nested conditions
    bool is_group = false;
    std::vector<condition> children;
};

// =============================================================================
// Order by clause
// =============================================================================

export struct order_by {
    std::string column;
    order_dir direction = order_dir::asc;
};

// =============================================================================
// query_wrapper — Fluent API for building WHERE conditions
// =============================================================================

export template <Model T>
class query_wrapper {
public:
    query_wrapper() = default;

    // =========================================================================
    // Comparison methods
    // =========================================================================

    auto eq(std::string_view column, const auto& value) -> query_wrapper& {
        add_condition(column, compare_op::eq, detail::to_param_value(value));
        return *this;
    }

    auto ne(std::string_view column, const auto& value) -> query_wrapper& {
        add_condition(column, compare_op::ne, detail::to_param_value(value));
        return *this;
    }

    auto gt(std::string_view column, const auto& value) -> query_wrapper& {
        add_condition(column, compare_op::gt, detail::to_param_value(value));
        return *this;
    }

    auto ge(std::string_view column, const auto& value) -> query_wrapper& {
        add_condition(column, compare_op::ge, detail::to_param_value(value));
        return *this;
    }

    auto lt(std::string_view column, const auto& value) -> query_wrapper& {
        add_condition(column, compare_op::lt, detail::to_param_value(value));
        return *this;
    }

    auto le(std::string_view column, const auto& value) -> query_wrapper& {
        add_condition(column, compare_op::le, detail::to_param_value(value));
        return *this;
    }

    auto like(std::string_view column, std::string_view pattern) -> query_wrapper& {
        add_condition(column, compare_op::like, param_value::from_string(std::string(pattern)));
        return *this;
    }

    auto not_like(std::string_view column, std::string_view pattern) -> query_wrapper& {
        add_condition(column, compare_op::not_like, param_value::from_string(std::string(pattern)));
        return *this;
    }

    auto is_null(std::string_view column) -> query_wrapper& {
        add_condition(column, compare_op::is_null, {});
        return *this;
    }

    auto is_not_null(std::string_view column) -> query_wrapper& {
        add_condition(column, compare_op::is_not_null, {});
        return *this;
    }

    template <typename ValueType>
    auto in(std::string_view column, const std::vector<ValueType>& values) -> query_wrapper& {
        std::vector<param_value> params;
        for (auto& v : values) {
            params.push_back(detail::to_param_value(v));
        }
        add_condition(column, compare_op::in, std::move(params));
        return *this;
    }

    template <typename ValueType>
    auto not_in(std::string_view column, const std::vector<ValueType>& values) -> query_wrapper& {
        std::vector<param_value> params;
        for (auto& v : values) {
            params.push_back(detail::to_param_value(v));
        }
        add_condition(column, compare_op::not_in, std::move(params));
        return *this;
    }

    auto between(std::string_view column, const auto& start, const auto& end) -> query_wrapper& {
        std::vector<param_value> params;
        params.push_back(detail::to_param_value(start));
        params.push_back(detail::to_param_value(end));
        add_condition(column, compare_op::between, std::move(params));
        return *this;
    }

    // =========================================================================
    // Logical operators
    // =========================================================================

    auto and_() -> query_wrapper& {
        current_logic_ = logic_op::and_op;
        return *this;
    }

    auto or_() -> query_wrapper& {
        current_logic_ = logic_op::or_op;
        return *this;
    }

    // Nested conditions: wrapper.and_(nested_wrapper)
    auto and_(const query_wrapper& nested) -> query_wrapper& {
        condition cond;
        cond.is_group = true;
        cond.children = nested.conditions_;
        cond.connector = logic_op::and_op;
        conditions_.push_back(std::move(cond));
        return *this;
    }

    auto or_(const query_wrapper& nested) -> query_wrapper& {
        condition cond;
        cond.is_group = true;
        cond.children = nested.conditions_;
        cond.connector = logic_op::or_op;
        conditions_.push_back(std::move(cond));
        return *this;
    }

    // =========================================================================
    // ORDER BY
    // =========================================================================

    auto order_by_asc(std::string_view column) -> query_wrapper& {
        order_by_.push_back({std::string(column), order_dir::asc});
        return *this;
    }

    auto order_by_desc(std::string_view column) -> query_wrapper& {
        order_by_.push_back({std::string(column), order_dir::desc});
        return *this;
    }

    // =========================================================================
    // LIMIT / OFFSET
    // =========================================================================

    auto limit(std::int64_t count) -> query_wrapper& {
        limit_ = count;
        return *this;
    }

    auto offset(std::int64_t count) -> query_wrapper& {
        offset_ = count;
        return *this;
    }

    // =========================================================================
    // SELECT columns
    // =========================================================================

    auto select(std::initializer_list<std::string_view> columns) -> query_wrapper& {
        select_columns_.clear();
        for (auto col : columns) {
            select_columns_.push_back(std::string(col));
        }
        return *this;
    }

    // =========================================================================
    // GROUP BY / HAVING
    // =========================================================================

    auto group_by(std::string_view column) -> query_wrapper& {
        group_by_.push_back(std::string(column));
        return *this;
    }

    auto having(std::string_view condition) -> query_wrapper& {
        having_ = condition;
        return *this;
    }

    // =========================================================================
    // Build SQL
    // =========================================================================

    auto build_select_sql() const -> std::pair<std::string, std::vector<param_value>> {
        auto& meta = model_traits<T>::meta();
        std::string sql = "SELECT ";

        // SELECT columns
        if (select_columns_.empty()) {
            sql += "*";
        } else {
            for (std::size_t i = 0; i < select_columns_.size(); ++i) {
                if (i > 0) sql += ", ";
                sql += std::format("`{}`", select_columns_[i]);
            }
        }

        sql += std::format(" FROM `{}`", meta.table_name);

        // WHERE clause
        std::vector<param_value> params;
        if (!conditions_.empty()) {
            sql += " WHERE ";
            sql += build_where_clause(conditions_, params);
        }

        // GROUP BY
        if (!group_by_.empty()) {
            sql += " GROUP BY ";
            for (std::size_t i = 0; i < group_by_.size(); ++i) {
                if (i > 0) sql += ", ";
                sql += std::format("`{}`", group_by_[i]);
            }
        }

        // HAVING
        if (!having_.empty()) {
            sql += std::format(" HAVING {}", having_);
        }

        // ORDER BY
        if (!order_by_.empty()) {
            sql += " ORDER BY ";
            for (std::size_t i = 0; i < order_by_.size(); ++i) {
                if (i > 0) sql += ", ";
                sql += std::format("`{}` {}", order_by_[i].column,
                    order_by_[i].direction == order_dir::asc ? "ASC" : "DESC");
            }
        }

        // LIMIT / OFFSET
        if (limit_ > 0) {
            sql += std::format(" LIMIT {}", limit_);
            if (offset_ > 0) {
                sql += std::format(" OFFSET {}", offset_);
            }
        }

        return {sql, params};
    }

    auto build_count_sql() const -> std::pair<std::string, std::vector<param_value>> {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("SELECT COUNT(*) FROM `{}`", meta.table_name);

        std::vector<param_value> params;
        if (!conditions_.empty()) {
            sql += " WHERE ";
            sql += build_where_clause(conditions_, params);
        }

        return {sql, params};
    }

    auto build_delete_sql() const -> std::pair<std::string, std::vector<param_value>> {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("DELETE FROM `{}`", meta.table_name);

        std::vector<param_value> params;
        if (!conditions_.empty()) {
            sql += " WHERE ";
            sql += build_where_clause(conditions_, params);
        }

        return {sql, params};
    }

    auto build_update_sql(const T& entity) const -> std::pair<std::string, std::vector<param_value>> {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);

        std::vector<param_value> params;
        bool first = true;
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, col_flag::primary_key)) continue;
            if (has_flag(field.col.flags, col_flag::auto_increment)) continue;

            if (!first) sql += ", ";
            sql += std::format("`{}` = {{}}", field.col.column_name);
            params.push_back(field.getter(entity));
            first = false;
        }

        if (!conditions_.empty()) {
            sql += " WHERE ";
            sql += build_where_clause(conditions_, params);
        }

        return {sql, params};
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    auto conditions() const noexcept -> const std::vector<condition>& { return conditions_; }
    auto is_empty() const noexcept -> bool { return conditions_.empty(); }

private:
    std::vector<condition> conditions_;
    std::vector<order_by> order_by_;
    std::vector<std::string> select_columns_;
    std::vector<std::string> group_by_;
    std::string having_;
    std::int64_t limit_ = 0;
    std::int64_t offset_ = 0;
    logic_op current_logic_ = logic_op::and_op;

    void add_condition(std::string_view column, compare_op op, std::vector<param_value> values) {
        condition cond;
        cond.column = column;
        cond.op = op;
        cond.values = std::move(values);
        cond.connector = current_logic_;
        conditions_.push_back(std::move(cond));
        current_logic_ = logic_op::and_op; // Reset to AND
    }

    void add_condition(std::string_view column, compare_op op, param_value value) {
        std::vector<param_value> values;
        values.push_back(std::move(value));
        add_condition(column, op, std::move(values));
    }

    static auto build_where_clause(const std::vector<condition>& conds,
                                    std::vector<param_value>& params) -> std::string {
        std::string sql;
        for (std::size_t i = 0; i < conds.size(); ++i) {
            auto& cond = conds[i];

            // Add logical connector
            if (i > 0) {
                sql += cond.connector == logic_op::and_op ? " AND " : " OR ";
            }

            // Nested group
            if (cond.is_group) {
                sql += "(";
                sql += build_where_clause(cond.children, params);
                sql += ")";
                continue;
            }

            // Build condition
            sql += std::format("`{}`", cond.column);

            switch (cond.op) {
            case compare_op::eq:
                sql += " = {}";
                params.push_back(cond.values[0]);
                break;
            case compare_op::ne:
                sql += " != {}";
                params.push_back(cond.values[0]);
                break;
            case compare_op::gt:
                sql += " > {}";
                params.push_back(cond.values[0]);
                break;
            case compare_op::ge:
                sql += " >= {}";
                params.push_back(cond.values[0]);
                break;
            case compare_op::lt:
                sql += " < {}";
                params.push_back(cond.values[0]);
                break;
            case compare_op::le:
                sql += " <= {}";
                params.push_back(cond.values[0]);
                break;
            case compare_op::like:
                sql += " LIKE {}";
                params.push_back(cond.values[0]);
                break;
            case compare_op::not_like:
                sql += " NOT LIKE {}";
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
                for (std::size_t j = 0; j < cond.values.size(); ++j) {
                    if (j > 0) sql += ", ";
                    sql += "{}";
                    params.push_back(cond.values[j]);
                }
                sql += ")";
                break;
            case compare_op::not_in:
                sql += " NOT IN (";
                for (std::size_t j = 0; j < cond.values.size(); ++j) {
                    if (j > 0) sql += ", ";
                    sql += "{}";
                    params.push_back(cond.values[j]);
                }
                sql += ")";
                break;
            case compare_op::between:
                sql += " BETWEEN {} AND {}";
                params.push_back(cond.values[0]);
                params.push_back(cond.values[1]);
                break;
            }
        }
        return sql;
    }
};

// =============================================================================
// update_wrapper — Fluent API for UPDATE operations
// =============================================================================

export template <Model T>
class update_wrapper {
public:
    update_wrapper() = default;

    // Set field value
    auto set(std::string_view column, const auto& value) -> update_wrapper& {
        set_fields_[std::string(column)] = detail::to_param_value(value);
        return *this;
    }

    // WHERE conditions (reuse query_wrapper logic)
    auto eq(std::string_view column, const auto& value) -> update_wrapper& {
        where_.eq(column, value);
        return *this;
    }

    auto ne(std::string_view column, const auto& value) -> update_wrapper& {
        where_.ne(column, value);
        return *this;
    }

    auto gt(std::string_view column, const auto& value) -> update_wrapper& {
        where_.gt(column, value);
        return *this;
    }

    template <typename ValueType>
    auto in(std::string_view column, const std::vector<ValueType>& values) -> update_wrapper& {
        where_.in(column, values);
        return *this;
    }

    // Build UPDATE SQL
    auto build_sql() const -> std::pair<std::string, std::vector<param_value>> {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);

        std::vector<param_value> params;
        bool first = true;
        for (auto& [col, val] : set_fields_) {
            if (!first) sql += ", ";
            sql += std::format("`{}` = {{}}", col);
            params.push_back(val);
            first = false;
        }

        if (!where_.is_empty()) {
            sql += " WHERE ";
            auto [where_sql, where_params] = where_.build_select_sql();
            // Extract WHERE clause from SELECT SQL
            auto where_pos = where_sql.find(" WHERE ");
            if (where_pos != std::string::npos) {
                auto where_clause = where_sql.substr(where_pos + 7);
                // Remove ORDER BY, LIMIT, etc.
                auto order_pos = where_clause.find(" ORDER BY");
                if (order_pos != std::string::npos) {
                    where_clause = where_clause.substr(0, order_pos);
                }
                sql += where_clause;
                params.insert(params.end(), where_params.begin(), where_params.end());
            }
        }

        return {sql, params};
    }

private:
    std::unordered_map<std::string, param_value> set_fields_;
    query_wrapper<T> where_;
};

} // namespace cnetmod::mysql::orm
