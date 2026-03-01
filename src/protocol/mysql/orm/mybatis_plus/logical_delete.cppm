export module cnetmod.protocol.mysql:orm_logical_delete;

import std;
import :types;
import :orm_meta;

namespace cnetmod::mysql::orm {

// =============================================================================
// Logical delete field flags
// =============================================================================

export constexpr col_flag LOGIC_DELETE = static_cast<col_flag>(0x10);

// =============================================================================
// logical_delete_config — Configuration for logical delete
// =============================================================================

export struct logical_delete_config {
    std::string field_name = "deleted";     // Field name for logical delete flag
    param_value deleted_value = param_value::from_int(1);    // Value when deleted
    param_value not_deleted_value = param_value::from_int(0); // Value when not deleted
    bool enabled = true;                    // Enable/disable logical delete globally
};

// =============================================================================
// logical_delete_interceptor — Intercepts SQL to add logical delete conditions
// =============================================================================

export class logical_delete_interceptor {
public:
    explicit logical_delete_interceptor(logical_delete_config config = {})
        : config_(std::move(config)) {}

    /// Check if model has logical delete field
    template <Model T>
    auto has_logical_delete() const -> bool {
        if (!config_.enabled) return false;

        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, LOGIC_DELETE)) return true;
            if (field.col.column_name == config_.field_name) return true;
        }
        return false;
    }

    /// Get logical delete field name for model
    template <Model T>
    auto get_delete_field() const -> std::optional<std::string> {
        if (!config_.enabled) return std::nullopt;

        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, LOGIC_DELETE)) return std::string(field.col.column_name);
            if (field.col.column_name == config_.field_name) return std::string(field.col.column_name);
        }
        return std::nullopt;
    }

    /// Inject WHERE condition for SELECT queries
    /// Transforms: SELECT * FROM users WHERE id = 1
    /// To:         SELECT * FROM users WHERE id = 1 AND deleted = 0
    template <Model T>
    auto inject_select_condition(std::string sql) const -> std::string {
        auto field = get_delete_field<T>();
        if (!field) return sql;

        // Find WHERE clause
        auto where_pos = sql.find(" WHERE ");
        if (where_pos != std::string::npos) {
            // Has WHERE clause - append AND condition
            auto insert_pos = where_pos + 7; // After " WHERE "

            // Find the end of WHERE clause (before ORDER BY, GROUP BY, LIMIT, etc.)
            // (Currently not used, but kept for potential future use)

            // Insert at the beginning of WHERE clause
            std::string condition = std::format("`{}` = {} AND ", *field,
                config_.not_deleted_value.kind == param_value::kind_t::int64_kind
                    ? std::to_string(config_.not_deleted_value.int_val)
                    : "0");

            sql.insert(insert_pos, condition);
        } else {
            // No WHERE clause - add one before ORDER BY, GROUP BY, LIMIT
            auto order_pos = sql.find(" ORDER BY");
            auto group_pos = sql.find(" GROUP BY");
            auto limit_pos = sql.find(" LIMIT");

            auto insert_pos = std::min({
                order_pos != std::string::npos ? order_pos : sql.size(),
                group_pos != std::string::npos ? group_pos : sql.size(),
                limit_pos != std::string::npos ? limit_pos : sql.size()
            });

            std::string condition = std::format(" WHERE `{}` = {}",
                *field,
                config_.not_deleted_value.kind == param_value::kind_t::int64_kind
                    ? std::to_string(config_.not_deleted_value.int_val)
                    : "0");

            sql.insert(insert_pos, condition);
        }

        return sql;
    }

    /// Transform DELETE to UPDATE for logical delete
    /// Transforms: DELETE FROM users WHERE id = 1
    /// To:         UPDATE users SET deleted = 1 WHERE id = 1
    template <Model T>
    auto transform_delete_to_update(std::string sql) const -> std::string {
        auto field = get_delete_field<T>();
        if (!field) return sql;

        // Check if it's a DELETE statement
        if (!sql.starts_with("DELETE FROM") && !sql.starts_with("delete from")) {
            return sql;
        }

        auto& meta = model_traits<T>::meta();

        // Extract WHERE clause
        auto where_pos = sql.find(" WHERE ");
        std::string where_clause;
        if (where_pos != std::string::npos) {
            where_clause = sql.substr(where_pos);
        }

        // Build UPDATE statement
        std::string update_sql = std::format("UPDATE `{}` SET `{}` = {}{}",
            meta.table_name,
            *field,
            config_.deleted_value.kind == param_value::kind_t::int64_kind
                ? std::to_string(config_.deleted_value.int_val)
                : "1",
            where_clause);

        return update_sql;
    }

    /// Get configuration
    auto config() const noexcept -> const logical_delete_config& { return config_; }

    /// Set configuration
    void set_config(logical_delete_config config) { config_ = std::move(config); }

    /// Enable/disable logical delete
    void set_enabled(bool enabled) { config_.enabled = enabled; }

private:
    logical_delete_config config_;
};

// =============================================================================
// Global logical delete interceptor instance
// =============================================================================

export inline logical_delete_interceptor& global_logical_delete_interceptor() {
    static logical_delete_interceptor instance;
    return instance;
}

// =============================================================================
// Helper macros for defining logical delete fields
// =============================================================================

// Usage in CNETMOD_MODEL:
// CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)

} // namespace cnetmod::mysql::orm
