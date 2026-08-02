export module cnetmod.orm.query_wrapper;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.model_metadata;
import cnetmod.orm.model_reflection;
import cnetmod.orm.member_pointer_reflection;

namespace cnetmod::orm {

// =============================================================================
// Comparison operators
// =============================================================================

export enum class compare_op
{
    eq,          // =
    ne,          // !=
    gt,          // >
    ge,          // >=
    lt,          // <
    le,          // <=
    like,        // LIKE
    not_like,    // NOT LIKE
    in,          // IN
    not_in,      // NOT IN
    is_null,     // IS NULL
    is_not_null, // IS NOT NULL
    between,     // BETWEEN
    not_between, // NOT BETWEEN
    is_true,     // IS TRUE
    is_false,    // IS FALSE
    raw,         // Raw SQL fragment (use with caution)
};

// =============================================================================
// Logical operators
// =============================================================================

export enum class logic_op
{
    and_op,
    or_op,
};

// =============================================================================
// Order direction
// =============================================================================

export enum class order_dir
{
    asc,
    desc,
};

// =============================================================================
// JOIN type
// =============================================================================

export enum class join_type
{
    inner,
    left,
    right,
    full_outer,
    cross,
};

// =============================================================================
// Aggregate function
// =============================================================================

export enum class aggregate_func
{
    count,
    sum,
    avg,
    min,
    max,
    count_distinct,
};

// =============================================================================
// Condition node
// =============================================================================

export struct condition
{
    std::string column;
    compare_op op = compare_op::eq;
    std::vector<param_value> values;
    logic_op connector = logic_op::and_op;

    /// For nested conditions
    bool is_group = false;
    std::vector<condition> children;
};

// =============================================================================
// Order by clause
// =============================================================================

export struct order_by
{
    std::string column;
    order_dir direction = order_dir::asc;
};

// =============================================================================
// JOIN clause
// =============================================================================

export struct join_clause
{
    join_type type = join_type::inner;
    std::string table;
    std::string condition; ///< ON condition (raw SQL, user handles quoting)
};

// =============================================================================
// Aggregate column definition
// =============================================================================

export struct aggregate_column
{
    aggregate_func func = aggregate_func::count;
    std::string column;
    std::string alias; ///< AS alias (empty = no alias)
};

// =============================================================================
// Aggregate HAVING condition (structured, parameterised)
// =============================================================================

export struct aggregate_having
{
    aggregate_func func = aggregate_func::count;
    std::string column;
    std::string op; ///< Comparison operator string: ">", ">=", "<", "<=", "=", "!="
    param_value value;
    logic_op connector = logic_op::and_op;
};

// =============================================================================
// query_wrapper — Fluent API for building SQL queries
// =============================================================================

export template <Model T>
class query_wrapper
{
public:
    query_wrapper() = default;

    // =========================================================================
    // Comparison methods  (string column name + auto value)
    // =========================================================================

    /// @brief WHERE column = value
    auto eq(std::string_view column, const auto& value) -> query_wrapper&
    {
        add_condition(column, compare_op::eq, to_query_parameter(value));
        return *this;
    }

    /// @brief WHERE column != value
    auto ne(std::string_view column, const auto& value) -> query_wrapper&
    {
        add_condition(column, compare_op::ne, to_query_parameter(value));
        return *this;
    }

    /// @brief WHERE column > value
    auto gt(std::string_view column, const auto& value) -> query_wrapper&
    {
        add_condition(column, compare_op::gt, to_query_parameter(value));
        return *this;
    }

    /// @brief WHERE column >= value
    auto ge(std::string_view column, const auto& value) -> query_wrapper&
    {
        add_condition(column, compare_op::ge, to_query_parameter(value));
        return *this;
    }

    /// @brief WHERE column < value
    auto lt(std::string_view column, const auto& value) -> query_wrapper&
    {
        add_condition(column, compare_op::lt, to_query_parameter(value));
        return *this;
    }

    /// @brief WHERE column <= value
    auto le(std::string_view column, const auto& value) -> query_wrapper&
    {
        add_condition(column, compare_op::le, to_query_parameter(value));
        return *this;
    }

    /// @brief WHERE column LIKE pattern
    auto like(std::string_view column, std::string_view pattern) -> query_wrapper&;

    /// @brief WHERE column NOT LIKE pattern
    auto not_like(std::string_view column, std::string_view pattern) -> query_wrapper&;

    /// @brief WHERE column IS NULL
    auto is_null(std::string_view column) -> query_wrapper&;

    /// @brief WHERE column IS NOT NULL
    auto is_not_null(std::string_view column) -> query_wrapper&;

    /// @brief WHERE column IN (values...)
    template <typename ValueType>
    auto in(std::string_view column, const std::vector<ValueType>& values) -> query_wrapper&
    {
        std::vector<param_value> params;
        for (auto& v : values)
            params.push_back(to_query_parameter(v));
        add_condition(column, compare_op::in, std::move(params));
        return *this;
    }

    /// @brief WHERE column NOT IN (values...)
    template <typename ValueType>
    auto not_in(std::string_view column, const std::vector<ValueType>& values) -> query_wrapper&
    {
        std::vector<param_value> params;
        for (auto& v : values)
            params.push_back(to_query_parameter(v));
        add_condition(column, compare_op::not_in, std::move(params));
        return *this;
    }

    /// @brief WHERE column BETWEEN start AND end
    auto between(std::string_view column, const auto& start, const auto& end) -> query_wrapper&
    {
        std::vector<param_value> params;
        params.push_back(to_query_parameter(start));
        params.push_back(to_query_parameter(end));
        add_condition(column, compare_op::between, std::move(params));
        return *this;
    }

    /// @brief WHERE column NOT BETWEEN start AND end
    auto not_between(std::string_view column, const auto& start, const auto& end) -> query_wrapper&
    {
        std::vector<param_value> params;
        params.push_back(to_query_parameter(start));
        params.push_back(to_query_parameter(end));
        add_condition(column, compare_op::not_between, std::move(params));
        return *this;
    }

    // =========================================================================
    // Boolean field checks
    // =========================================================================

    /// @brief WHERE column IS TRUE  (or = 1 for MySQL)
    auto is_true(std::string_view column) -> query_wrapper&;

    /// @brief WHERE column IS FALSE (or = 0 for MySQL)
    auto is_false(std::string_view column) -> query_wrapper&;

    // =========================================================================
    // Raw SQL fragment (use with caution — no SQL injection protection)
    // =========================================================================

    /// @brief Append a raw SQL fragment to the WHERE clause
    auto raw(std::string_view raw_sql) -> query_wrapper&;

    // =========================================================================
    // LIKE pattern helpers
    // =========================================================================

    /// @brief WHERE column LIKE 'prefix%'
    auto starts_with(std::string_view column, std::string_view prefix) -> query_wrapper&;

    /// @brief WHERE column LIKE '%suffix'
    auto ends_with(std::string_view column, std::string_view suffix) -> query_wrapper&;

    /// @brief WHERE column LIKE '%substring%'
    auto contains(std::string_view column, std::string_view substring) -> query_wrapper&;

    // =========================================================================
    // Conditional execution
    // =========================================================================

    /// @brief Apply builder_fn only when @p condition is true
    template <typename Fn>
    auto when(bool condition, Fn&& builder_fn) -> query_wrapper&
    {
        if (condition)
            builder_fn(*this);
        return *this;
    }

    // =========================================================================
    // Logical operators
    // =========================================================================

    /// @brief Switch the next condition connector to AND (default)
    auto and_() -> query_wrapper&;

    /// @brief Switch the next condition connector to OR
    auto or_() -> query_wrapper&;

    /// @brief Append a nested condition group connected with AND
    auto and_(const query_wrapper& nested) -> query_wrapper&;

    /// @brief Append a nested condition group connected with OR
    auto or_(const query_wrapper& nested) -> query_wrapper&;

    // =========================================================================
    // ORDER BY
    // =========================================================================

    /// @brief ORDER BY column ASC
    auto order_by_asc(std::string_view column) -> query_wrapper&;

    /// @brief ORDER BY column DESC
    auto order_by_desc(std::string_view column) -> query_wrapper&;

    // =========================================================================
    // LIMIT / OFFSET
    // =========================================================================

    /// @brief Set LIMIT count
    auto limit(std::int64_t count) -> query_wrapper&;

    /// @brief Set OFFSET count
    auto offset(std::int64_t count) -> query_wrapper&;

    // =========================================================================
    // SELECT columns
    // =========================================================================

    /// @brief Select specific columns (initializer-list form, clears previous)
    auto select(std::initializer_list<std::string_view> columns) -> query_wrapper&;

    /// @brief Select specific columns (variadic string form, appends)
    template <typename... Cols>
    requires (std::convertible_to<Cols, std::string_view> && ...)
    auto select(Cols... cols) -> query_wrapper&
    {
        (select_columns_.emplace_back(std::string_view(cols)), ...);
        return *this;
    }

    // =========================================================================
    // GROUP BY / HAVING
    // =========================================================================

    /// @brief GROUP BY single column
    auto group_by(std::string_view column) -> query_wrapper&;

    /// @brief GROUP BY multiple columns
    auto group_by(std::initializer_list<std::string_view> columns) -> query_wrapper&;

    /// @brief Raw HAVING clause (user-written SQL, no parameterisation)
    auto having(std::string_view condition) -> query_wrapper&;

    /// @brief Structured HAVING with aggregate function and parameterised value
    auto having(aggregate_func func, std::string_view column,
                std::string_view op, const auto& value) -> query_wrapper&
    {
        aggregate_having h;
        h.func = func;
        h.column = std::string(column);
        h.op = std::string(op);
        h.value = to_query_parameter(value);
        h.connector = current_logic_;
        having_conditions_.push_back(std::move(h));
        return *this;
    }

    // =========================================================================
    // JOIN
    // =========================================================================

    /// @brief Add a JOIN clause (raw ON condition, user handles identifier quoting)
    auto join(std::string_view table, std::string_view condition,
              join_type type = join_type::inner) -> query_wrapper&;

    /// @brief Convenience: INNER JOIN
    auto inner_join(std::string_view table, std::string_view condition) -> query_wrapper&;

    /// @brief Convenience: LEFT JOIN
    auto left_join(std::string_view table, std::string_view condition) -> query_wrapper&;

    /// @brief Convenience: RIGHT JOIN
    auto right_join(std::string_view table, std::string_view condition) -> query_wrapper&;

    /// @brief Convenience: FULL OUTER JOIN
    auto full_outer_join(std::string_view table, std::string_view condition) -> query_wrapper&;

    // =========================================================================
    // Aggregate SELECT
    // =========================================================================

    /// @brief Add an aggregate column to the SELECT list
    auto select_aggregate(aggregate_func func, std::string_view column,
                          std::string_view alias = "") -> query_wrapper&;

    /// @brief SELECT COUNT(column) AS alias
    auto select_count(std::string_view column = "*",
                      std::string_view alias = "count") -> query_wrapper&;

    /// @brief SELECT SUM(column) AS alias
    auto select_sum(std::string_view column,
                    std::string_view alias = "") -> query_wrapper&;

    /// @brief SELECT AVG(column) AS alias
    auto select_avg(std::string_view column,
                    std::string_view alias = "") -> query_wrapper&;

    /// @brief SELECT MIN(column) AS alias
    auto select_min(std::string_view column,
                    std::string_view alias = "") -> query_wrapper&;

    /// @brief SELECT MAX(column) AS alias
    auto select_max(std::string_view column,
                    std::string_view alias = "") -> query_wrapper&;

    // =========================================================================
    // Build SQL — dialect-aware overloads
    // =========================================================================

    /// @brief Build SELECT SQL with default (MySQL) dialect
    auto build_select_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build SELECT SQL with specified dialect
    auto build_select_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build SELECT SQL with explicit dialect config
    auto build_select_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build COUNT SQL with default (MySQL) dialect
    auto build_count_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build COUNT SQL with specified dialect
    auto build_count_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build COUNT SQL with explicit dialect config
    auto build_count_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build DELETE SQL with default (MySQL) dialect
    auto build_delete_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build DELETE SQL with specified dialect
    auto build_delete_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build DELETE SQL with explicit dialect config
    auto build_delete_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL from entity with default (MySQL) dialect
    auto build_update_sql(const T& entity) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL from entity with specified dialect
    auto build_update_sql(const T& entity, sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL from entity with explicit dialect config
    auto build_update_sql(const T& entity, const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    // =========================================================================
    // Accessors
    // =========================================================================

    /// @brief Read-only access to the accumulated conditions
    auto conditions() const noexcept -> const std::vector<condition>&;

    /// @brief True when no conditions have been added
    auto is_empty() const noexcept -> bool;

    // =========================================================================
    // Stand-alone WHERE clause builder (used by update_wrapper / external code)
    // =========================================================================

    /// @brief Build WHERE clause SQL with default (MySQL) dialect
    auto build_where_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build WHERE clause SQL with specified dialect
    auto build_where_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build WHERE clause SQL with explicit dialect config and optional starting param index
    auto build_where_sql(const dialect_config& cfg, int initial_param_index = 0) const
        -> std::pair<std::string, std::vector<param_value>>;

    // =========================================================================
    // Member pointer overloads — type-safe column reference via U T::*
    // =========================================================================

    template <typename U>
    auto eq(U T::*member_ptr, const auto& value) -> query_wrapper&
    { return eq(resolve_column_name<T>(member_ptr), value); }

    template <typename U>
    auto ne(U T::*member_ptr, const auto& value) -> query_wrapper&
    { return ne(resolve_column_name<T>(member_ptr), value); }

    template <typename U>
    auto gt(U T::*member_ptr, const auto& value) -> query_wrapper&
    { return gt(resolve_column_name<T>(member_ptr), value); }

    template <typename U>
    auto ge(U T::*member_ptr, const auto& value) -> query_wrapper&
    { return ge(resolve_column_name<T>(member_ptr), value); }

    template <typename U>
    auto lt(U T::*member_ptr, const auto& value) -> query_wrapper&
    { return lt(resolve_column_name<T>(member_ptr), value); }

    template <typename U>
    auto le(U T::*member_ptr, const auto& value) -> query_wrapper&
    { return le(resolve_column_name<T>(member_ptr), value); }

    template <typename U>
    auto like(U T::*member_ptr, std::string_view pattern) -> query_wrapper&
    { return like(resolve_column_name<T>(member_ptr), pattern); }

    template <typename U>
    auto not_like(U T::*member_ptr, std::string_view pattern) -> query_wrapper&
    { return not_like(resolve_column_name<T>(member_ptr), pattern); }

    template <typename U>
    auto is_null(U T::*member_ptr) -> query_wrapper&
    { return is_null(resolve_column_name<T>(member_ptr)); }

    template <typename U>
    auto is_not_null(U T::*member_ptr) -> query_wrapper&
    { return is_not_null(resolve_column_name<T>(member_ptr)); }

    template <typename U, typename ValueType>
    auto in(U T::*member_ptr, const std::vector<ValueType>& values) -> query_wrapper&
    { return in(resolve_column_name<T>(member_ptr), values); }

    template <typename U, typename ValueType>
    auto not_in(U T::*member_ptr, const std::vector<ValueType>& values) -> query_wrapper&
    { return not_in(resolve_column_name<T>(member_ptr), values); }

    template <typename U>
    auto between(U T::*member_ptr, const auto& start, const auto& end) -> query_wrapper&
    { return between(resolve_column_name<T>(member_ptr), start, end); }

    template <typename U>
    auto not_between(U T::*member_ptr, const auto& start, const auto& end) -> query_wrapper&
    { return not_between(resolve_column_name<T>(member_ptr), start, end); }

    template <typename U>
    auto is_true(U T::*member_ptr) -> query_wrapper&
    { return is_true(resolve_column_name<T>(member_ptr)); }

    template <typename U>
    auto is_false(U T::*member_ptr) -> query_wrapper&
    { return is_false(resolve_column_name<T>(member_ptr)); }

    template <typename U>
    auto order_by_asc(U T::*member_ptr) -> query_wrapper&
    { return order_by_asc(resolve_column_name<T>(member_ptr)); }

    template <typename U>
    auto order_by_desc(U T::*member_ptr) -> query_wrapper&
    { return order_by_desc(resolve_column_name<T>(member_ptr)); }

    /// @brief Select columns via member pointers (type-safe partial select)
    template <typename... Members>
    auto select(Members T::*... member_ptrs) -> query_wrapper&
    {
        (select_columns_.push_back(std::string(resolve_column_name<T>(member_ptrs))), ...);
        return *this;
    }

    template <typename U>
    auto group_by(U T::*member_ptr) -> query_wrapper&
    { return group_by(resolve_column_name<T>(member_ptr)); }

    template <typename U>
    auto select_count(U T::*member_ptr, std::string_view alias = "count") -> query_wrapper&
    { return select_count(resolve_column_name<T>(member_ptr), alias); }

    template <typename U>
    auto select_sum(U T::*member_ptr, std::string_view alias = "") -> query_wrapper&
    { return select_sum(resolve_column_name<T>(member_ptr), alias); }

    template <typename U>
    auto select_avg(U T::*member_ptr, std::string_view alias = "") -> query_wrapper&
    { return select_avg(resolve_column_name<T>(member_ptr), alias); }

    template <typename U>
    auto select_min(U T::*member_ptr, std::string_view alias = "") -> query_wrapper&
    { return select_min(resolve_column_name<T>(member_ptr), alias); }

    template <typename U>
    auto select_max(U T::*member_ptr, std::string_view alias = "") -> query_wrapper&
    { return select_max(resolve_column_name<T>(member_ptr), alias); }

private:
    std::vector<condition> conditions_;
    std::vector<order_by> order_by_;
    std::vector<std::string> select_columns_;
    std::vector<std::string> group_by_;
    std::string having_;
    std::int64_t limit_ = 0;
    std::int64_t offset_ = 0;
    logic_op current_logic_ = logic_op::and_op;

    // JOIN support
    std::vector<join_clause> joins_;

    // Aggregate support
    std::vector<aggregate_column> aggregate_columns_;
    std::vector<aggregate_having> having_conditions_;

    // -- private helpers (implemented in query_wrapper_impl.cpp) --

    void add_condition(std::string_view column, compare_op op, std::vector<param_value> values);
    void add_condition(std::string_view column, compare_op op, param_value value);

    static auto build_where_clause(const std::vector<condition>& conds,
        std::vector<param_value>& params,
        const dialect_config& cfg,
        int& param_index) -> std::string;
};

// =============================================================================
// update_wrapper — Fluent API for UPDATE operations
// =============================================================================

export template <Model T>
class update_wrapper
{
public:
    update_wrapper() = default;

    /// @brief Set a field value by column name
    auto set(std::string_view column, const auto& value) -> update_wrapper&
    {
        set_fields_[std::string(column)] = to_query_parameter(value);
        return *this;
    }

    /// @brief Set a field value by member pointer (type-safe)
    template <typename U>
    auto set(U T::*member_ptr, const auto& value) -> update_wrapper&
    {
        return set(resolve_column_name<T>(member_ptr), value);
    }

    // -- WHERE conditions (delegate to internal query_wrapper) --

    auto eq(std::string_view column, const auto& value) -> update_wrapper&
    { where_.eq(column, value); return *this; }

    auto ne(std::string_view column, const auto& value) -> update_wrapper&
    { where_.ne(column, value); return *this; }

    auto gt(std::string_view column, const auto& value) -> update_wrapper&
    { where_.gt(column, value); return *this; }

    auto ge(std::string_view column, const auto& value) -> update_wrapper&
    { where_.ge(column, value); return *this; }

    auto lt(std::string_view column, const auto& value) -> update_wrapper&
    { where_.lt(column, value); return *this; }

    auto le(std::string_view column, const auto& value) -> update_wrapper&
    { where_.le(column, value); return *this; }

    template <typename ValueType>
    auto in(std::string_view column, const std::vector<ValueType>& values) -> update_wrapper&
    { where_.in(column, values); return *this; }

    auto like(std::string_view column, std::string_view pattern) -> update_wrapper&
    { where_.like(column, pattern); return *this; }

    auto is_null(std::string_view column) -> update_wrapper&
    { where_.is_null(column); return *this; }

    auto is_not_null(std::string_view column) -> update_wrapper&
    { where_.is_not_null(column); return *this; }

    auto between(std::string_view column, const auto& start, const auto& end) -> update_wrapper&
    { where_.between(column, start, end); return *this; }

    // -- Member pointer WHERE overloads --

    template <typename U>
    auto eq(U T::*member_ptr, const auto& value) -> update_wrapper&
    { where_.eq(resolve_column_name<T>(member_ptr), value); return *this; }

    template <typename U>
    auto ne(U T::*member_ptr, const auto& value) -> update_wrapper&
    { where_.ne(resolve_column_name<T>(member_ptr), value); return *this; }

    template <typename U>
    auto gt(U T::*member_ptr, const auto& value) -> update_wrapper&
    { where_.gt(resolve_column_name<T>(member_ptr), value); return *this; }

    template <typename U>
    auto ge(U T::*member_ptr, const auto& value) -> update_wrapper&
    { where_.ge(resolve_column_name<T>(member_ptr), value); return *this; }

    template <typename U>
    auto lt(U T::*member_ptr, const auto& value) -> update_wrapper&
    { where_.lt(resolve_column_name<T>(member_ptr), value); return *this; }

    template <typename U>
    auto le(U T::*member_ptr, const auto& value) -> update_wrapper&
    { where_.le(resolve_column_name<T>(member_ptr), value); return *this; }

    template <typename U, typename ValueType>
    auto in(U T::*member_ptr, const std::vector<ValueType>& values) -> update_wrapper&
    { where_.in(resolve_column_name<T>(member_ptr), values); return *this; }

    template <typename U>
    auto like(U T::*member_ptr, std::string_view pattern) -> update_wrapper&
    { where_.like(resolve_column_name<T>(member_ptr), pattern); return *this; }

    template <typename U>
    auto is_null(U T::*member_ptr) -> update_wrapper&
    { where_.is_null(resolve_column_name<T>(member_ptr)); return *this; }

    template <typename U>
    auto is_not_null(U T::*member_ptr) -> update_wrapper&
    { where_.is_not_null(resolve_column_name<T>(member_ptr)); return *this; }

    template <typename U>
    auto between(U T::*member_ptr, const auto& start, const auto& end) -> update_wrapper&
    { where_.between(resolve_column_name<T>(member_ptr), start, end); return *this; }

    // -- Build SQL --

    /// @brief Build UPDATE SQL with default (MySQL) dialect
    auto build_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL with specified dialect
    auto build_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL with explicit dialect config
    auto build_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

private:
    std::unordered_map<std::string, param_value> set_fields_;
    query_wrapper<T> where_;
};

} // namespace cnetmod::orm

// Template member definitions must be reachable by importers so wrappers can
// be instantiated for application-defined model types.
#include "query_wrapper_impl.inc"
