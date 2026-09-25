export module cnetmod.orm.multi_tenant;

import std;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_metadata;
import cnetmod.orm.interceptor_chain;
import cnetmod.orm.sql_dialect;

namespace cnetmod::orm {

// =============================================================================
// Multi-tenant field flag
// =============================================================================

export constexpr col_flag TENANT_ID = col_flag::tenant_id;

/**
 * Request-owned authorization snapshot. The caller resolves tenant ancestry;
 * the ORM enforces the exact read/write sets on every mapped statement.
 * Empty sets deny access. `tenant_id` is the default target of an INSERT that
 * omits the tenant column and must itself be in writable_tenant_ids.
 */
export struct tenant_scope
{
    std::int64_t tenant_id{};
    std::vector<std::int64_t> readable_tenant_ids;
    std::vector<std::int64_t> writable_tenant_ids;

    [[nodiscard]] static auto self(std::int64_t id) -> tenant_scope
    {
        return {id, {id}, {id}};
    }
};

/** Strict, fail-closed SQL policy for a single tenant-mapped table. */
export auto apply_tenant_scope(sql_operation operation,
    intercepted_statement statement, std::string_view table,
    std::string_view tenant_column, const tenant_scope& scope,
    sql_dialect dialect)
    -> std::expected<intercepted_statement, std::string>;

/** Validates one bound INSERT column against an exact authorized ID set. */
export auto validate_bound_insert_column(intercepted_statement statement,
    std::string_view table, std::string_view column,
    const std::vector<std::int64_t>& allowed_ids, sql_dialect dialect)
    -> std::expected<intercepted_statement, std::string>;

// =============================================================================
// tenant_context — Thread-local tenant context
// =============================================================================

export class tenant_context
{
public:
    static void set_tenant_id(std::int64_t tenant_id);

    static auto get_tenant_id() -> std::optional<std::int64_t>;

    static void clear();

private:
    static thread_local std::optional<std::int64_t> current_tenant_id_;
};

// =============================================================================
// multi_tenant_interceptor — Intercepts SQL to add tenant_id conditions
// =============================================================================

export class multi_tenant_interceptor
{
public:
    explicit multi_tenant_interceptor(std::string tenant_field = "tenant_id");

    /// Check if model has tenant_id field
    template <Model T> auto has_tenant_field() const -> bool
    {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, TENANT_ID))
                return true;
            if (field.col.column_name == tenant_field_)
                return true;
        }
        return false;
    }

    /// Inject tenant_id condition into SELECT/UPDATE/DELETE
    /// Transforms: SELECT * FROM users WHERE id = 1
    /// To:         SELECT * FROM users WHERE tenant_id = ? AND id = 1
    template <Model T>
    auto inject_tenant_condition(std::string sql,
        std::vector<param_value>& params) const
        -> std::string
    {
        if (!has_tenant_field<T>())
            return sql;

        auto tenant_id = tenant_context::get_tenant_id();
        if (!tenant_id)
            return sql; // No tenant context, skip
        return inject_tenant_condition_impl(std::move(sql), params, tenant_field_,
            *tenant_id);
    }

    /// Inject tenant_id into INSERT
    /// Transforms: INSERT INTO users (name, email) VALUES (?, ?)
    /// To:         INSERT INTO users (name, email, tenant_id) VALUES (?, ?, ?)
    template <Model T>
    auto inject_tenant_insert(std::string sql,
        std::vector<param_value>& params) const
        -> std::string
    {
        if (!has_tenant_field<T>())
            return sql;

        auto tenant_id = tenant_context::get_tenant_id();
        if (!tenant_id)
            return sql;
        return inject_tenant_insert_impl(std::move(sql), params, tenant_field_,
            *tenant_id);
    }

    /// Set tenant field name
    void set_tenant_field(std::string field);

    /// Get tenant field name
    auto tenant_field() const noexcept -> const std::string&;

private:
    static auto inject_tenant_condition_impl(std::string sql,
        std::vector<param_value>& params,
        std::string_view tenant_field,
        std::int64_t tenant_id)
        -> std::string;
    static auto inject_tenant_insert_impl(std::string sql,
        std::vector<param_value>& params,
        std::string_view tenant_field,
        std::int64_t tenant_id) -> std::string;
    std::string tenant_field_;
};

// =============================================================================
// Global multi-tenant interceptor instance
// =============================================================================

export auto global_multi_tenant_interceptor() -> multi_tenant_interceptor&;

// =============================================================================
// RAII tenant context guard
// =============================================================================

export class tenant_guard
{
public:
    explicit tenant_guard(std::int64_t tenant_id);

    ~tenant_guard();

    tenant_guard(const tenant_guard&) = delete;
    tenant_guard& operator=(const tenant_guard&) = delete;
};

} // namespace cnetmod::orm
