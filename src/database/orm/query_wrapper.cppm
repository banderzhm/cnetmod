export module cnetmod.orm.query_wrapper;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.model_metadata;
import cnetmod.orm.model_reflection;
import cnetmod.orm.member_pointer_reflection;

namespace cnetmod::orm {

template <class Value>
concept query_parameter_compatible = requires(const Value& value) {
    { to_query_parameter(value) } -> std::same_as<param_value>;
};

namespace query_type_detail {

    template <class T> struct optional_value
    {
        using type = std::remove_cvref_t<T>;
        static constexpr bool is_optional = false;
    };

    template <class T> struct optional_value<std::optional<T>>
    {
        using type = std::remove_cv_t<T>;
        static constexpr bool is_optional = true;
    };

    template <class T>
    using value_type = typename optional_value<std::remove_cvref_t<T>>::type;

    template <class T>
    inline constexpr bool is_optional =
        optional_value<std::remove_cvref_t<T>>::is_optional;

    template <class T>
    inline constexpr bool is_nullopt =
        std::same_as<std::remove_cvref_t<T>, std::nullopt_t>;

    template <class T>
    concept string_like = std::convertible_to<T, std::string_view>;

    template <class Member, class Value>
    inline constexpr bool base_values_are_compatible = []
    {
        using member_type = value_type<Member>;
        using value = value_type<Value>;
        if constexpr (std::same_as<member_type, value>)
            return true;
        else if constexpr (std::same_as<member_type, bool> ||
            std::same_as<value, bool>)
            return false;
        else if constexpr (std::integral<member_type> && std::integral<value>)
            return true;
        else if constexpr (std::floating_point<member_type> &&
            (std::integral<value> || std::floating_point<value>))
            return true;
        else if constexpr (string_like<member_type> && string_like<value>)
            return true;
        else
            return false;
    }();

} // namespace query_type_detail

template <class Member, class Value>
concept member_predicate_value = query_parameter_compatible<Value> &&
    (query_type_detail::is_nullopt<Value> ||
        query_type_detail::base_values_are_compatible<Member, Value>);

template <class Member, class Value>
concept member_assignment_value = query_parameter_compatible<Value> &&
    ((!query_type_detail::is_nullopt<Value> &&
         !query_type_detail::is_optional<Value>) ||
        query_type_detail::is_optional<Member>) &&
    (query_type_detail::is_nullopt<Value> ||
        query_type_detail::base_values_are_compatible<Member, Value>);

template <class Member>
concept string_member = query_type_detail::string_like<
    query_type_detail::value_type<Member>>;

template <class Member>
concept boolean_member = std::same_as<
    query_type_detail::value_type<Member>, bool>;

template <class Member>
concept numeric_member =
    (std::integral<query_type_detail::value_type<Member>> &&
        !boolean_member<Member>) ||
    std::floating_point<query_type_detail::value_type<Member>>;

/**
 * @brief Explicit escape hatch for a column name only known at runtime.
 *
 * Model-backed application code should use a member pointer. This wrapper
 * keeps dynamic reports and framework metadata visibly distinct from typed
 * model queries.
 */
export struct runtime_column
{
    std::string_view name;
};

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
    in_subquery,
    not_in_subquery,
    exists_subquery,
    not_exists_subquery,
    eq_subquery,
    ne_subquery,
    gt_subquery,
    ge_subquery,
    lt_subquery,
    le_subquery,
};

/**
 * @brief Parameterized subquery fragment used by the structured predicates.
 */
export struct subquery
{
    std::string sql;
    std::vector<param_value> parameters;
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

/// Operation selected when a query_wrapper is submitted directly to a
/// database_session. Query wrappers default to a read, so a broad DELETE is
/// never selected accidentally.
export enum class query_execution_kind
{
    select,
    delete_,
};

/**
 * @brief Selects how an update assignment obtains its value.
 */
export enum class update_value_kind : std::uint8_t
{
    bound,
    increment,
    decrement,
};

export struct update_assignment
{
    std::string column;
    param_value value;
    update_value_kind kind = update_value_kind::bound;
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
    std::optional<subquery> nested_query;
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

    /// Mark this wrapper for direct DELETE execution through
    /// database_session::execute. The predicates remain unchanged.
    auto as_delete() noexcept -> query_wrapper&
    {
        execution_kind_ = query_execution_kind::delete_;
        return *this;
    }

    /// Mark this wrapper for direct SELECT execution. This is the default.
    auto as_select() noexcept -> query_wrapper&
    {
        execution_kind_ = query_execution_kind::select;
        return *this;
    }

    [[nodiscard]] auto execution_kind() const noexcept -> query_execution_kind
    {
        return execution_kind_;
    }

    // =========================================================================
    // Comparison methods  (string column name + auto value)
    // =========================================================================

    /// @brief WHERE a runtime-selected column = value
    template <query_parameter_compatible Value>
    auto eq(runtime_column column, const Value& value) -> query_wrapper&
    {
        add_condition(column.name, compare_op::eq, to_query_parameter(value));
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto eq(std::string_view column, const Value& value) -> query_wrapper&
    {
        return eq(runtime_column{column}, value);
    }

    /// @brief WHERE column != value
    template <query_parameter_compatible Value>
    auto ne(runtime_column column, const Value& value) -> query_wrapper&
    {
        add_condition(column.name, compare_op::ne, to_query_parameter(value));
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto ne(std::string_view column, const Value& value) -> query_wrapper&
    {
        return ne(runtime_column{column}, value);
    }

    /// @brief WHERE column > value
    template <query_parameter_compatible Value>
    auto gt(runtime_column column, const Value& value) -> query_wrapper&
    {
        add_condition(column.name, compare_op::gt, to_query_parameter(value));
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto gt(std::string_view column, const Value& value) -> query_wrapper&
    {
        return gt(runtime_column{column}, value);
    }

    /// @brief WHERE column >= value
    template <query_parameter_compatible Value>
    auto ge(runtime_column column, const Value& value) -> query_wrapper&
    {
        add_condition(column.name, compare_op::ge, to_query_parameter(value));
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto ge(std::string_view column, const Value& value) -> query_wrapper&
    {
        return ge(runtime_column{column}, value);
    }

    /// @brief WHERE column < value
    template <query_parameter_compatible Value>
    auto lt(runtime_column column, const Value& value) -> query_wrapper&
    {
        add_condition(column.name, compare_op::lt, to_query_parameter(value));
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto lt(std::string_view column, const Value& value) -> query_wrapper&
    {
        return lt(runtime_column{column}, value);
    }

    /// @brief WHERE column <= value
    template <query_parameter_compatible Value>
    auto le(runtime_column column, const Value& value) -> query_wrapper&
    {
        add_condition(column.name, compare_op::le, to_query_parameter(value));
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto le(std::string_view column, const Value& value) -> query_wrapper&
    {
        return le(runtime_column{column}, value);
    }

    /**
     * @brief Adds equality predicates for every supplied column and value.
     *
     * Null values become `IS NULL` by default. Set @p null_as_is_null to false
     * to omit null entries. Values remain bound parameters.
     */
    auto all_eq(std::span<const std::pair<std::string, param_value>> values,
        bool null_as_is_null = true) -> query_wrapper&
    {
        for (const auto& [column, value] : values)
        {
            if (value.kind == param_value::kind_t::null_kind)
            {
                if (null_as_is_null)
                    is_null(runtime_column{column});
                continue;
            }
            eq(runtime_column{column}, value);
        }
        return *this;
    }

    /// @brief WHERE a runtime-selected column LIKE pattern
    auto like(runtime_column column, std::string_view pattern) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto like(std::string_view column, std::string_view pattern) -> query_wrapper&
    {
        return like(runtime_column{column}, pattern);
    }

    /// @brief WHERE a runtime-selected column NOT LIKE pattern
    auto not_like(runtime_column column, std::string_view pattern) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto not_like(std::string_view column, std::string_view pattern) -> query_wrapper&
    {
        return not_like(runtime_column{column}, pattern);
    }

    /// @brief WHERE a runtime-selected column IS NULL
    auto is_null(runtime_column column) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_null(std::string_view column) -> query_wrapper&
    {
        return is_null(runtime_column{column});
    }

    /// @brief WHERE a runtime-selected column IS NOT NULL
    auto is_not_null(runtime_column column) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_not_null(std::string_view column) -> query_wrapper&
    {
        return is_not_null(runtime_column{column});
    }

    /// @brief WHERE column IN (values...)
    template <query_parameter_compatible ValueType>
    auto in(runtime_column column, const std::vector<ValueType>& values) -> query_wrapper&
    {
        std::vector<param_value> params;
        for (auto& v : values)
            params.push_back(to_query_parameter(v));
        add_condition(column.name, compare_op::in, std::move(params));
        return *this;
    }

    template <query_parameter_compatible ValueType>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto in(std::string_view column, const std::vector<ValueType>& values) -> query_wrapper&
    {
        return in(runtime_column{column}, values);
    }

    /// @brief WHERE column NOT IN (values...)
    template <query_parameter_compatible ValueType>
    auto not_in(runtime_column column, const std::vector<ValueType>& values) -> query_wrapper&
    {
        std::vector<param_value> params;
        for (auto& v : values)
            params.push_back(to_query_parameter(v));
        add_condition(column.name, compare_op::not_in, std::move(params));
        return *this;
    }

    template <query_parameter_compatible ValueType>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto not_in(std::string_view column, const std::vector<ValueType>& values) -> query_wrapper&
    {
        return not_in(runtime_column{column}, values);
    }

    /// @brief WHERE column BETWEEN start AND end
    template <query_parameter_compatible Start, query_parameter_compatible End>
    auto between(runtime_column column, const Start& start, const End& end) -> query_wrapper&
    {
        std::vector<param_value> params;
        params.push_back(to_query_parameter(start));
        params.push_back(to_query_parameter(end));
        add_condition(column.name, compare_op::between, std::move(params));
        return *this;
    }

    template <query_parameter_compatible Start, query_parameter_compatible End>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto between(std::string_view column, const Start& start, const End& end) -> query_wrapper&
    {
        return between(runtime_column{column}, start, end);
    }

    /// @brief WHERE column NOT BETWEEN start AND end
    template <query_parameter_compatible Start, query_parameter_compatible End>
    auto not_between(runtime_column column, const Start& start, const End& end) -> query_wrapper&
    {
        std::vector<param_value> params;
        params.push_back(to_query_parameter(start));
        params.push_back(to_query_parameter(end));
        add_condition(column.name, compare_op::not_between, std::move(params));
        return *this;
    }

    template <query_parameter_compatible Start, query_parameter_compatible End>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto not_between(std::string_view column, const Start& start, const End& end) -> query_wrapper&
    {
        return not_between(runtime_column{column}, start, end);
    }

    /** @brief Adds `column IN (parameterized subquery)`. */
    auto in_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        condition value;
        value.column = std::string(column.name);
        value.op = compare_op::in_subquery;
        value.nested_query = std::move(query);
        value.connector = current_logic_;
        conditions_.push_back(std::move(value));
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto in_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return in_subquery(runtime_column{column}, std::move(query));
    }

    /** @brief Adds `column NOT IN (parameterized subquery)`. */
    auto not_in_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        condition value;
        value.column = std::string(column.name);
        value.op = compare_op::not_in_subquery;
        value.nested_query = std::move(query);
        value.connector = current_logic_;
        conditions_.push_back(std::move(value));
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto not_in_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return not_in_subquery(runtime_column{column}, std::move(query));
    }

    /** @brief Adds a parameterized EXISTS subquery. */
    auto exists(subquery query) -> query_wrapper&
    {
        condition value;
        value.op = compare_op::exists_subquery;
        value.nested_query = std::move(query);
        value.connector = current_logic_;
        conditions_.push_back(std::move(value));
        return *this;
    }

    /** @brief Adds a parameterized NOT EXISTS subquery. */
    auto not_exists(subquery query) -> query_wrapper&
    {
        condition value;
        value.op = compare_op::not_exists_subquery;
        value.nested_query = std::move(query);
        value.connector = current_logic_;
        conditions_.push_back(std::move(value));
        return *this;
    }

    /** @brief Adds `column = (parameterized subquery)`. */
    auto eq_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        return add_scalar_subquery(column.name, compare_op::eq_subquery,
            std::move(query));
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto eq_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return eq_subquery(runtime_column{column}, std::move(query));
    }

    /** @brief Adds `column != (parameterized subquery)`. */
    auto ne_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        return add_scalar_subquery(column.name, compare_op::ne_subquery,
            std::move(query));
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto ne_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return ne_subquery(runtime_column{column}, std::move(query));
    }

    /** @brief Adds `column > (parameterized subquery)`. */
    auto gt_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        return add_scalar_subquery(column.name, compare_op::gt_subquery,
            std::move(query));
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto gt_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return gt_subquery(runtime_column{column}, std::move(query));
    }

    /** @brief Adds `column >= (parameterized subquery)`. */
    auto ge_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        return add_scalar_subquery(column.name, compare_op::ge_subquery,
            std::move(query));
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto ge_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return ge_subquery(runtime_column{column}, std::move(query));
    }

    /** @brief Adds `column < (parameterized subquery)`. */
    auto lt_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        return add_scalar_subquery(column.name, compare_op::lt_subquery,
            std::move(query));
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto lt_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return lt_subquery(runtime_column{column}, std::move(query));
    }

    /** @brief Adds `column <= (parameterized subquery)`. */
    auto le_subquery(runtime_column column, subquery query) -> query_wrapper&
    {
        return add_scalar_subquery(column.name, compare_op::le_subquery,
            std::move(query));
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto le_subquery(std::string_view column, subquery query) -> query_wrapper&
    {
        return le_subquery(runtime_column{column}, std::move(query));
    }

    // =========================================================================
    // Boolean field checks
    // =========================================================================

    /// @brief WHERE column IS TRUE  (or = 1 for MySQL)
    auto is_true(runtime_column column) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_true(std::string_view column) -> query_wrapper&
    {
        return is_true(runtime_column{column});
    }

    /// @brief WHERE column IS FALSE (or = 0 for MySQL)
    auto is_false(runtime_column column) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_false(std::string_view column) -> query_wrapper&
    {
        return is_false(runtime_column{column});
    }

    // =========================================================================
    // Raw SQL fragment (use with caution — no SQL injection protection)
    // =========================================================================

    /// @brief Append a raw SQL fragment to the WHERE clause
    auto raw(std::string_view raw_sql) -> query_wrapper&;

    // =========================================================================
    // LIKE pattern helpers
    // =========================================================================

    /// @brief WHERE column LIKE 'prefix%'
    auto starts_with(runtime_column column, std::string_view prefix) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto starts_with(std::string_view column, std::string_view prefix) -> query_wrapper&
    {
        return starts_with(runtime_column{column}, prefix);
    }

    /// @brief WHERE column LIKE '%suffix'
    auto ends_with(runtime_column column, std::string_view suffix) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto ends_with(std::string_view column, std::string_view suffix) -> query_wrapper&
    {
        return ends_with(runtime_column{column}, suffix);
    }

    /// @brief WHERE column LIKE '%substring%'
    auto contains(runtime_column column, std::string_view substring) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto contains(std::string_view column, std::string_view substring) -> query_wrapper&
    {
        return contains(runtime_column{column}, substring);
    }

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
    auto order_by_asc(runtime_column column) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto order_by_asc(std::string_view column) -> query_wrapper&
    {
        return order_by_asc(runtime_column{column});
    }

    /// @brief ORDER BY column DESC
    auto order_by_desc(runtime_column column) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto order_by_desc(std::string_view column) -> query_wrapper&
    {
        return order_by_desc(runtime_column{column});
    }

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

    /// @brief Select explicitly dynamic columns (initializer-list form, clears previous)
    auto select(std::span<const runtime_column> columns) -> query_wrapper&;

    [[deprecated("use model member pointers or runtime_column for dynamic SQL")]] auto select(std::initializer_list<std::string_view> columns) -> query_wrapper&
    {
        select_columns_.clear();
        for (auto column : columns)
            select_columns_.emplace_back(column);
        return *this;
    }

    /// @brief Select explicitly dynamic columns (variadic form, appends)
    template <typename... Cols>
    requires(sizeof...(Cols) > 0 &&
        (std::same_as<std::remove_cvref_t<Cols>, runtime_column> && ...))
    auto select(Cols... cols) -> query_wrapper&
    {
        (select_columns_.emplace_back(cols.name), ...);
        return *this;
    }

    // =========================================================================
    // GROUP BY / HAVING
    // =========================================================================

    /// @brief GROUP BY single column
    auto group_by(runtime_column column) -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto group_by(std::string_view column) -> query_wrapper&
    {
        return group_by(runtime_column{column});
    }

    /// @brief GROUP BY multiple columns
    auto group_by(std::span<const runtime_column> columns) -> query_wrapper&;

    [[deprecated("use model member pointers or runtime_column for dynamic SQL")]] auto group_by(std::initializer_list<std::string_view> columns) -> query_wrapper&
    {
        for (auto column : columns)
            group_by(runtime_column{column});
        return *this;
    }

    /// @brief Raw HAVING clause (user-written SQL, no parameterisation)
    auto having(std::string_view condition) -> query_wrapper&;

    /// @brief Structured HAVING with aggregate function and parameterised value
    auto having(aggregate_func func, runtime_column column,
        std::string_view op, const auto& value) -> query_wrapper&
    {
        aggregate_having h;
        h.func = func;
        h.column = std::string(column.name);
        h.op = std::string(op);
        h.value = to_query_parameter(value);
        h.connector = current_logic_;
        having_conditions_.push_back(std::move(h));
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto having(aggregate_func func, std::string_view column,
        std::string_view op, const Value& value) -> query_wrapper&
    {
        return having(func, runtime_column{column}, op, value);
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
    auto select_aggregate(aggregate_func func, runtime_column column,
        std::string_view alias = "") -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto select_aggregate(aggregate_func func, std::string_view column,
        std::string_view alias = "") -> query_wrapper&
    {
        return select_aggregate(func, runtime_column{column}, alias);
    }

    /// @brief SELECT COUNT(column) AS alias
    auto select_count() -> query_wrapper&
    {
        return select_aggregate(aggregate_func::count, runtime_column{"*"}, "count");
    }

    auto select_count(runtime_column column,
        std::string_view alias = "count") -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto select_count(std::string_view column, std::string_view alias = "count") -> query_wrapper&
    {
        return select_count(runtime_column{column}, alias);
    }

    /// @brief SELECT SUM(column) AS alias
    auto select_sum(runtime_column column,
        std::string_view alias = "") -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto select_sum(std::string_view column, std::string_view alias = "") -> query_wrapper&
    {
        return select_sum(runtime_column{column}, alias);
    }

    /// @brief SELECT AVG(column) AS alias
    auto select_avg(runtime_column column,
        std::string_view alias = "") -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto select_avg(std::string_view column, std::string_view alias = "") -> query_wrapper&
    {
        return select_avg(runtime_column{column}, alias);
    }

    /// @brief SELECT MIN(column) AS alias
    auto select_min(runtime_column column,
        std::string_view alias = "") -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto select_min(std::string_view column, std::string_view alias = "") -> query_wrapper&
    {
        return select_min(runtime_column{column}, alias);
    }

    /// @brief SELECT MAX(column) AS alias
    auto select_max(runtime_column column,
        std::string_view alias = "") -> query_wrapper&;

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto select_max(std::string_view column, std::string_view alias = "") -> query_wrapper&
    {
        return select_max(runtime_column{column}, alias);
    }

    // =========================================================================
    // Build SQL — dialect-aware overloads
    // =========================================================================

    /// @brief Build SELECT SQL with default (MySQL) dialect
    auto build_select_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build SELECT SQL with specified dialect
    auto build_select_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build SELECT SQL with explicit dialect config
    auto build_select_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /**
     * @brief Builds SELECT SQL for a routed physical table.
     *
     * The caller owns table routing. The identifier is always quoted by the
     * selected dialect and values remain bound parameters.
     */
    auto build_select_sql(std::string_view physical_table, const dialect_config& cfg) const
        -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build COUNT SQL with default (MySQL) dialect
    auto build_count_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build COUNT SQL with specified dialect
    auto build_count_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build COUNT SQL with explicit dialect config
    auto build_count_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /** @brief Builds COUNT SQL for a routed physical table. */
    auto build_count_sql(std::string_view physical_table, const dialect_config& cfg) const
        -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build DELETE SQL with default (MySQL) dialect
    auto build_delete_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build DELETE SQL with specified dialect
    auto build_delete_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build DELETE SQL with explicit dialect config
    auto build_delete_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /** @brief Builds DELETE SQL for a routed physical table. */
    auto build_delete_sql(std::string_view physical_table, const dialect_config& cfg) const
        -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL from entity with default (MySQL) dialect
    auto build_update_sql(const T& entity) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL from entity with specified dialect
    auto build_update_sql(const T& entity, sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL from entity with explicit dialect config
    auto build_update_sql(const T& entity, const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /** @brief Builds entity UPDATE SQL for a routed physical table. */
    auto build_update_sql(const T& entity, std::string_view physical_table,
        const dialect_config& cfg) const
        -> std::pair<std::string, std::vector<param_value>>;

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

    template <typename U, member_predicate_value<U> Value>
    auto eq(U T::* member_ptr, const Value& value) -> query_wrapper&
    {
        return eq(runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    template <typename U, member_predicate_value<U> Value>
    auto ne(U T::* member_ptr, const Value& value) -> query_wrapper&
    {
        return ne(runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    template <typename U, member_predicate_value<U> Value>
    auto gt(U T::* member_ptr, const Value& value) -> query_wrapper&
    {
        return gt(runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    template <typename U, member_predicate_value<U> Value>
    auto ge(U T::* member_ptr, const Value& value) -> query_wrapper&
    {
        return ge(runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    template <typename U, member_predicate_value<U> Value>
    auto lt(U T::* member_ptr, const Value& value) -> query_wrapper&
    {
        return lt(runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    template <typename U, member_predicate_value<U> Value>
    auto le(U T::* member_ptr, const Value& value) -> query_wrapper&
    {
        return le(runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    template <string_member U>
    auto like(U T::* member_ptr, std::string_view pattern) -> query_wrapper&
    {
        return like(runtime_column{resolve_column_name<T>(member_ptr)}, pattern);
    }

    template <string_member U>
    auto not_like(U T::* member_ptr, std::string_view pattern) -> query_wrapper&
    {
        return not_like(runtime_column{resolve_column_name<T>(member_ptr)}, pattern);
    }

    template <typename U>
    auto is_null(U T::* member_ptr) -> query_wrapper&
    {
        return is_null(runtime_column{resolve_column_name<T>(member_ptr)});
    }

    template <typename U>
    auto is_not_null(U T::* member_ptr) -> query_wrapper&
    {
        return is_not_null(runtime_column{resolve_column_name<T>(member_ptr)});
    }

    template <typename U, member_predicate_value<U> ValueType>
    auto in(U T::* member_ptr, const std::vector<ValueType>& values) -> query_wrapper&
    {
        return in(runtime_column{resolve_column_name<T>(member_ptr)}, values);
    }

    template <typename U, member_predicate_value<U> ValueType>
    auto not_in(U T::* member_ptr, const std::vector<ValueType>& values) -> query_wrapper&
    {
        return not_in(runtime_column{resolve_column_name<T>(member_ptr)}, values);
    }

    template <typename U, member_predicate_value<U> Start,
        member_predicate_value<U> End>
    auto between(U T::* member_ptr, const Start& start, const End& end) -> query_wrapper&
    {
        return between(runtime_column{resolve_column_name<T>(member_ptr)}, start, end);
    }

    template <typename U, member_predicate_value<U> Start,
        member_predicate_value<U> End>
    auto not_between(U T::* member_ptr, const Start& start,
        const End& end) -> query_wrapper&
    {
        return not_between(runtime_column{resolve_column_name<T>(member_ptr)}, start, end);
    }

    template <boolean_member U>
    auto is_true(U T::* member_ptr) -> query_wrapper&
    {
        return is_true(runtime_column{resolve_column_name<T>(member_ptr)});
    }

    template <boolean_member U>
    auto is_false(U T::* member_ptr) -> query_wrapper&
    {
        return is_false(runtime_column{resolve_column_name<T>(member_ptr)});
    }

    template <typename U>
    auto in_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return in_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <typename U>
    auto not_in_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return not_in_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <typename U>
    auto eq_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return eq_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <typename U>
    auto ne_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return ne_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <typename U>
    auto gt_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return gt_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <typename U>
    auto ge_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return ge_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <typename U>
    auto lt_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return lt_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <typename U>
    auto le_subquery(U T::* member_ptr, subquery query) -> query_wrapper&
    {
        return le_subquery(runtime_column{resolve_column_name<T>(member_ptr)},
            std::move(query));
    }

    template <string_member U>
    auto starts_with(U T::* member_ptr, std::string_view prefix) -> query_wrapper&
    {
        return starts_with(runtime_column{resolve_column_name<T>(member_ptr)}, prefix);
    }

    template <string_member U>
    auto ends_with(U T::* member_ptr, std::string_view suffix) -> query_wrapper&
    {
        return ends_with(runtime_column{resolve_column_name<T>(member_ptr)}, suffix);
    }

    template <string_member U>
    auto contains(U T::* member_ptr, std::string_view substring) -> query_wrapper&
    {
        return contains(runtime_column{resolve_column_name<T>(member_ptr)}, substring);
    }

    template <typename U>
    auto order_by_asc(U T::* member_ptr) -> query_wrapper&
    {
        return order_by_asc(runtime_column{resolve_column_name<T>(member_ptr)});
    }

    template <typename U>
    auto order_by_desc(U T::* member_ptr) -> query_wrapper&
    {
        return order_by_desc(runtime_column{resolve_column_name<T>(member_ptr)});
    }

    /// @brief Select columns via member pointers (type-safe partial select)
    template <typename... Members>
    requires(sizeof...(Members) > 0)
    auto select(Members T::*... member_ptrs) -> query_wrapper&
    {
        (select_columns_.push_back(std::string(resolve_column_name<T>(member_ptrs))), ...);
        return *this;
    }

    template <typename U>
    auto group_by(U T::* member_ptr) -> query_wrapper&
    {
        return group_by(runtime_column{resolve_column_name<T>(member_ptr)});
    }

    template <typename U, query_parameter_compatible Value>
    auto having(aggregate_func func, U T::* member_ptr,
        std::string_view op, const Value& value) -> query_wrapper&
    {
        return having(func, runtime_column{resolve_column_name<T>(member_ptr)},
            op, value);
    }

    template <typename U>
    auto select_aggregate(aggregate_func func, U T::* member_ptr,
        std::string_view alias = "") -> query_wrapper&
    {
        return select_aggregate(func,
            runtime_column{resolve_column_name<T>(member_ptr)}, alias);
    }

    template <typename U>
    auto select_count(U T::* member_ptr, std::string_view alias = "count") -> query_wrapper&
    {
        return select_count(runtime_column{resolve_column_name<T>(member_ptr)}, alias);
    }

    template <typename U>
    auto select_sum(U T::* member_ptr, std::string_view alias = "") -> query_wrapper&
    {
        return select_sum(runtime_column{resolve_column_name<T>(member_ptr)}, alias);
    }

    template <typename U>
    auto select_avg(U T::* member_ptr, std::string_view alias = "") -> query_wrapper&
    {
        return select_avg(runtime_column{resolve_column_name<T>(member_ptr)}, alias);
    }

    template <typename U>
    auto select_min(U T::* member_ptr, std::string_view alias = "") -> query_wrapper&
    {
        return select_min(runtime_column{resolve_column_name<T>(member_ptr)}, alias);
    }

    template <typename U>
    auto select_max(U T::* member_ptr, std::string_view alias = "") -> query_wrapper&
    {
        return select_max(runtime_column{resolve_column_name<T>(member_ptr)}, alias);
    }

private:
    query_execution_kind execution_kind_ = query_execution_kind::select;
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

    auto add_scalar_subquery(std::string_view column, compare_op operation,
        subquery query) -> query_wrapper&
    {
        condition value;
        value.column = std::string(column);
        value.op = operation;
        value.nested_query = std::move(query);
        value.connector = current_logic_;
        conditions_.push_back(std::move(value));
        return *this;
    }

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

    /// @brief Set a field value selected dynamically at runtime
    template <query_parameter_compatible Value>
    auto set(runtime_column column, const Value& value) -> update_wrapper&
    {
        set_assignment(column.name, to_query_parameter(value), update_value_kind::bound);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto set(std::string_view column, const Value& value) -> update_wrapper&
    {
        return set(runtime_column{column}, value);
    }

    /// @brief Set a field value by member pointer (type-safe)
    template <typename U, member_assignment_value<U> Value>
    auto set(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        return set(runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    /** @brief Increments a numeric column by a bound value. */
    template <query_parameter_compatible Value>
    auto set_increment(runtime_column column, const Value& value)
        -> update_wrapper&
    {
        set_assignment(column.name, to_query_parameter(value),
            update_value_kind::increment);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto set_increment(std::string_view column, const Value& value)
        -> update_wrapper&
    {
        return set_increment(runtime_column{column}, value);
    }

    template <numeric_member U, member_assignment_value<U> Value>
    requires numeric_member<Value>
    auto set_increment(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        return set_increment(
            runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    /** @brief Decrements a numeric column by a bound value. */
    template <query_parameter_compatible Value>
    auto set_decrement(runtime_column column, const Value& value)
        -> update_wrapper&
    {
        set_assignment(column.name, to_query_parameter(value),
            update_value_kind::decrement);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto set_decrement(std::string_view column, const Value& value)
        -> update_wrapper&
    {
        return set_decrement(runtime_column{column}, value);
    }

    template <numeric_member U, member_assignment_value<U> Value>
    requires numeric_member<Value>
    auto set_decrement(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        return set_decrement(
            runtime_column{resolve_column_name<T>(member_ptr)}, value);
    }

    // -- WHERE conditions (delegate to internal query_wrapper) --

    template <query_parameter_compatible Value>
    auto eq(runtime_column column, const Value& value) -> update_wrapper&
    {
        where_.eq(column, value);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto eq(std::string_view column, const Value& value) -> update_wrapper&
    {
        return eq(runtime_column{column}, value);
    }

    template <query_parameter_compatible Value>
    auto ne(runtime_column column, const Value& value) -> update_wrapper&
    {
        where_.ne(column, value);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto ne(std::string_view column, const Value& value) -> update_wrapper&
    {
        return ne(runtime_column{column}, value);
    }

    template <query_parameter_compatible Value>
    auto gt(runtime_column column, const Value& value) -> update_wrapper&
    {
        where_.gt(column, value);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto gt(std::string_view column, const Value& value) -> update_wrapper&
    {
        return gt(runtime_column{column}, value);
    }

    template <query_parameter_compatible Value>
    auto ge(runtime_column column, const Value& value) -> update_wrapper&
    {
        where_.ge(column, value);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto ge(std::string_view column, const Value& value) -> update_wrapper&
    {
        return ge(runtime_column{column}, value);
    }

    template <query_parameter_compatible Value>
    auto lt(runtime_column column, const Value& value) -> update_wrapper&
    {
        where_.lt(column, value);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto lt(std::string_view column, const Value& value) -> update_wrapper&
    {
        return lt(runtime_column{column}, value);
    }

    template <query_parameter_compatible Value>
    auto le(runtime_column column, const Value& value) -> update_wrapper&
    {
        where_.le(column, value);
        return *this;
    }

    template <query_parameter_compatible Value>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto le(std::string_view column, const Value& value) -> update_wrapper&
    {
        return le(runtime_column{column}, value);
    }

    template <query_parameter_compatible ValueType>
    auto in(runtime_column column, const std::vector<ValueType>& values) -> update_wrapper&
    {
        where_.in(column, values);
        return *this;
    }

    template <query_parameter_compatible ValueType>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto in(std::string_view column, const std::vector<ValueType>& values)
        -> update_wrapper&
    {
        return in(runtime_column{column}, values);
    }

    template <query_parameter_compatible ValueType>
    auto not_in(runtime_column column, const std::vector<ValueType>& values)
        -> update_wrapper&
    {
        where_.not_in(column, values);
        return *this;
    }

    template <query_parameter_compatible ValueType>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto not_in(std::string_view column, const std::vector<ValueType>& values)
        -> update_wrapper&
    {
        return not_in(runtime_column{column}, values);
    }

    auto like(runtime_column column, std::string_view pattern) -> update_wrapper&
    {
        where_.like(column, pattern);
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto like(std::string_view column, std::string_view pattern) -> update_wrapper&
    {
        return like(runtime_column{column}, pattern);
    }

    auto not_like(runtime_column column, std::string_view pattern) -> update_wrapper&
    {
        where_.not_like(column, pattern);
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto not_like(std::string_view column, std::string_view pattern) -> update_wrapper&
    {
        return not_like(runtime_column{column}, pattern);
    }

    auto is_null(runtime_column column) -> update_wrapper&
    {
        where_.is_null(column);
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_null(std::string_view column) -> update_wrapper&
    {
        return is_null(runtime_column{column});
    }

    auto is_not_null(runtime_column column) -> update_wrapper&
    {
        where_.is_not_null(column);
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_not_null(std::string_view column) -> update_wrapper&
    {
        return is_not_null(runtime_column{column});
    }

    template <query_parameter_compatible Start, query_parameter_compatible End>
    auto between(runtime_column column, const Start& start, const End& end)
        -> update_wrapper&
    {
        where_.between(column, start, end);
        return *this;
    }

    template <query_parameter_compatible Start, query_parameter_compatible End>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto between(std::string_view column, const Start& start, const End& end)
        -> update_wrapper&
    {
        return between(runtime_column{column}, start, end);
    }

    template <query_parameter_compatible Start, query_parameter_compatible End>
    auto not_between(runtime_column column, const Start& start, const End& end)
        -> update_wrapper&
    {
        where_.not_between(column, start, end);
        return *this;
    }

    template <query_parameter_compatible Start, query_parameter_compatible End>
    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto not_between(std::string_view column, const Start& start, const End& end)
        -> update_wrapper&
    {
        return not_between(runtime_column{column}, start, end);
    }

    auto is_true(runtime_column column) -> update_wrapper&
    {
        where_.is_true(column);
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_true(std::string_view column) -> update_wrapper&
    {
        return is_true(runtime_column{column});
    }

    auto is_false(runtime_column column) -> update_wrapper&
    {
        where_.is_false(column);
        return *this;
    }

    [[deprecated("use a model member pointer or runtime_column for dynamic SQL")]] auto is_false(std::string_view column) -> update_wrapper&
    {
        return is_false(runtime_column{column});
    }

    // -- Member pointer WHERE overloads --

    template <typename U, member_predicate_value<U> Value>
    auto eq(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        where_.eq(runtime_column{resolve_column_name<T>(member_ptr)}, value);
        return *this;
    }

    template <typename U, member_predicate_value<U> Value>
    auto ne(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        where_.ne(runtime_column{resolve_column_name<T>(member_ptr)}, value);
        return *this;
    }

    template <typename U, member_predicate_value<U> Value>
    auto gt(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        where_.gt(runtime_column{resolve_column_name<T>(member_ptr)}, value);
        return *this;
    }

    template <typename U, member_predicate_value<U> Value>
    auto ge(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        where_.ge(runtime_column{resolve_column_name<T>(member_ptr)}, value);
        return *this;
    }

    template <typename U, member_predicate_value<U> Value>
    auto lt(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        where_.lt(runtime_column{resolve_column_name<T>(member_ptr)}, value);
        return *this;
    }

    template <typename U, member_predicate_value<U> Value>
    auto le(U T::* member_ptr, const Value& value) -> update_wrapper&
    {
        where_.le(runtime_column{resolve_column_name<T>(member_ptr)}, value);
        return *this;
    }

    template <typename U, member_predicate_value<U> ValueType>
    auto in(U T::* member_ptr, const std::vector<ValueType>& values) -> update_wrapper&
    {
        where_.in(runtime_column{resolve_column_name<T>(member_ptr)}, values);
        return *this;
    }

    template <typename U, member_predicate_value<U> ValueType>
    auto not_in(U T::* member_ptr, const std::vector<ValueType>& values) -> update_wrapper&
    {
        where_.not_in(runtime_column{resolve_column_name<T>(member_ptr)}, values);
        return *this;
    }

    template <string_member U>
    auto like(U T::* member_ptr, std::string_view pattern) -> update_wrapper&
    {
        where_.like(runtime_column{resolve_column_name<T>(member_ptr)}, pattern);
        return *this;
    }

    template <string_member U>
    auto not_like(U T::* member_ptr, std::string_view pattern) -> update_wrapper&
    {
        where_.not_like(runtime_column{resolve_column_name<T>(member_ptr)}, pattern);
        return *this;
    }

    template <typename U>
    auto is_null(U T::* member_ptr) -> update_wrapper&
    {
        where_.is_null(runtime_column{resolve_column_name<T>(member_ptr)});
        return *this;
    }

    template <typename U>
    auto is_not_null(U T::* member_ptr) -> update_wrapper&
    {
        where_.is_not_null(runtime_column{resolve_column_name<T>(member_ptr)});
        return *this;
    }

    template <typename U, member_predicate_value<U> Start,
        member_predicate_value<U> End>
    auto between(U T::* member_ptr, const Start& start, const End& end) -> update_wrapper&
    {
        where_.between(runtime_column{resolve_column_name<T>(member_ptr)}, start, end);
        return *this;
    }

    template <typename U, member_predicate_value<U> Start,
        member_predicate_value<U> End>
    auto not_between(U T::* member_ptr, const Start& start,
        const End& end) -> update_wrapper&
    {
        where_.not_between(runtime_column{resolve_column_name<T>(member_ptr)}, start, end);
        return *this;
    }

    template <boolean_member U>
    auto is_true(U T::* member_ptr) -> update_wrapper&
    {
        where_.is_true(runtime_column{resolve_column_name<T>(member_ptr)});
        return *this;
    }

    template <boolean_member U>
    auto is_false(U T::* member_ptr) -> update_wrapper&
    {
        where_.is_false(runtime_column{resolve_column_name<T>(member_ptr)});
        return *this;
    }

    // -- Build SQL --

    /// @brief Build UPDATE SQL with default (MySQL) dialect
    auto build_sql() const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL with specified dialect
    auto build_sql(sql_dialect dialect) const -> std::pair<std::string, std::vector<param_value>>;

    /// @brief Build UPDATE SQL with explicit dialect config
    auto build_sql(const dialect_config& cfg) const -> std::pair<std::string, std::vector<param_value>>;

    /** @brief Builds UPDATE SQL for a routed physical table. */
    auto build_sql(std::string_view physical_table, const dialect_config& cfg) const
        -> std::pair<std::string, std::vector<param_value>>;

    /**
     * @brief Reports whether the update has no WHERE predicates.
     */
    [[nodiscard]] auto has_conditions() const noexcept -> bool
    {
        return !where_.is_empty();
    }

    /**
     * @brief Reports whether at least one assignment was supplied.
     */
    [[nodiscard]] auto has_assignments() const noexcept -> bool
    {
        return !set_fields_.empty();
    }

    [[nodiscard]] auto has_assignment(std::string_view column) const noexcept -> bool
    {
        return std::ranges::any_of(set_fields_, [column](const auto& assignment)
            {
                return assignment.column == column;
            });
    }

    /** @brief Adds an automatic assignment without replacing a caller's value. */
    auto set_if_absent(std::string_view column, param_value value) -> update_wrapper&
    {
        if (!has_assignment(column))
            set_assignment(column, std::move(value), update_value_kind::bound);
        return *this;
    }

private:
    void set_assignment(std::string_view column, param_value value,
        update_value_kind kind)
    {
        for (auto& assignment : set_fields_)
        {
            if (assignment.column == column)
            {
                assignment.value = std::move(value);
                assignment.kind = kind;
                return;
            }
        }
        set_fields_.push_back({std::string(column), std::move(value), kind});
    }

    std::vector<update_assignment> set_fields_;
    query_wrapper<T> where_;
};

} // namespace cnetmod::orm

// Template member definitions must be reachable by importers so wrappers can
// be instantiated for application-defined model types.
#include "query_wrapper_impl.inc"
