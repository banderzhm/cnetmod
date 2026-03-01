export module cnetmod.protocol.mysql:orm_multi_tenant;

import std;
import :types;
import :orm_meta;

namespace cnetmod::mysql::orm {

// =============================================================================
// Multi-tenant field flag
// =============================================================================

export constexpr col_flag TENANT_ID = static_cast<col_flag>(0x40);

// =============================================================================
// tenant_context — Thread-local tenant context
// =============================================================================

export class tenant_context {
public:
    static void set_tenant_id(std::int64_t tenant_id) {
        current_tenant_id_ = tenant_id;
    }

    static auto get_tenant_id() -> std::optional<std::int64_t> {
        return current_tenant_id_;
    }

    static void clear() {
        current_tenant_id_ = std::nullopt;
    }

private:
    inline static thread_local std::optional<std::int64_t> current_tenant_id_;
};

// =============================================================================
// multi_tenant_interceptor — Intercepts SQL to add tenant_id conditions
// =============================================================================

export class multi_tenant_interceptor {
public:
    explicit multi_tenant_interceptor(std::string tenant_field = "tenant_id")
        : tenant_field_(std::move(tenant_field)) {}

    /// Check if model has tenant_id field
    template <Model T>
    auto has_tenant_field() const -> bool {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, TENANT_ID)) return true;
            if (field.col.column_name == tenant_field_) return true;
        }
        return false;
    }

    /// Inject tenant_id condition into SELECT/UPDATE/DELETE
    /// Transforms: SELECT * FROM users WHERE id = 1
    /// To:         SELECT * FROM users WHERE tenant_id = ? AND id = 1
    template <Model T>
    auto inject_tenant_condition(std::string sql, std::vector<param_value>& params) const -> std::string {
        if (!has_tenant_field<T>()) return sql;

        auto tenant_id = tenant_context::get_tenant_id();
        if (!tenant_id) return sql; // No tenant context, skip

        // Find WHERE clause
        auto where_pos = sql.find(" WHERE ");
        if (where_pos != std::string::npos) {
            // Has WHERE clause - prepend tenant condition
            auto insert_pos = where_pos + 7; // After " WHERE "

            std::string condition = std::format("`{}` = {} AND ", tenant_field_, "{}");
            sql.insert(insert_pos, condition);
            params.insert(params.begin(), param_value::from_int(*tenant_id));
        } else {
            // No WHERE clause - add one before ORDER BY, GROUP BY, LIMIT
            auto order_pos = sql.find(" ORDER BY");
            auto group_pos = sql.find(" GROUP BY");
            auto limit_pos = sql.find(" LIMIT");
            auto having_pos = sql.find(" HAVING");

            auto insert_pos = std::min({
                order_pos != std::string::npos ? order_pos : sql.size(),
                group_pos != std::string::npos ? group_pos : sql.size(),
                limit_pos != std::string::npos ? limit_pos : sql.size(),
                having_pos != std::string::npos ? having_pos : sql.size()
            });

            std::string condition = std::format(" WHERE `{}` = {}", tenant_field_, "{}");
            sql.insert(insert_pos, condition);
            params.push_back(param_value::from_int(*tenant_id));
        }

        return sql;
    }

    /// Inject tenant_id into INSERT
    /// Transforms: INSERT INTO users (name, email) VALUES (?, ?)
    /// To:         INSERT INTO users (name, email, tenant_id) VALUES (?, ?, ?)
    template <Model T>
    auto inject_tenant_insert(std::string sql, std::vector<param_value>& params) const -> std::string {
        if (!has_tenant_field<T>()) return sql;

        auto tenant_id = tenant_context::get_tenant_id();
        if (!tenant_id) return sql;

        // Find column list and VALUES clause
        auto values_pos = sql.find(" VALUES ");
        if (values_pos == std::string::npos) return sql;

        // Find column list (between first '(' and ')')
        auto col_start = sql.find('(');
        auto col_end = sql.find(')', col_start);
        if (col_start == std::string::npos || col_end == std::string::npos) return sql;

        // Add tenant_id column
        std::string tenant_col = std::format(", `{}`", tenant_field_);
        sql.insert(col_end, tenant_col);

        // Find VALUES clause end
        auto val_start = sql.find('(', values_pos);
        auto val_end = sql.find(')', val_start);
        if (val_start != std::string::npos && val_end != std::string::npos) {
            sql.insert(val_end, ", {}");
            params.push_back(param_value::from_int(*tenant_id));
        }

        return sql;
    }

    /// Set tenant field name
    void set_tenant_field(std::string field) { tenant_field_ = std::move(field); }

    /// Get tenant field name
    auto tenant_field() const noexcept -> const std::string& { return tenant_field_; }

private:
    std::string tenant_field_;
};

// =============================================================================
// Global multi-tenant interceptor instance
// =============================================================================

export inline multi_tenant_interceptor& global_multi_tenant_interceptor() {
    static multi_tenant_interceptor instance;
    return instance;
}

// =============================================================================
// RAII tenant context guard
// =============================================================================

export class tenant_guard {
public:
    explicit tenant_guard(std::int64_t tenant_id) {
        tenant_context::set_tenant_id(tenant_id);
    }

    ~tenant_guard() {
        tenant_context::clear();
    }

    tenant_guard(const tenant_guard&) = delete;
    tenant_guard& operator=(const tenant_guard&) = delete;
};

} // namespace cnetmod::mysql::orm
