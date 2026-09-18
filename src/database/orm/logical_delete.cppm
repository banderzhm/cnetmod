export module cnetmod.orm.logical_delete;

import std;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_metadata;

namespace cnetmod::orm {

// =============================================================================
// Logical delete field flags
// =============================================================================

export constexpr col_flag LOGIC_DELETE = static_cast<col_flag>(0x10);

// =============================================================================
// logical_delete_config — Configuration for logical delete
// =============================================================================

/**
 * @brief Selects how a logical deletion marker is represented.
 */
export enum class logical_delete_mode : std::uint8_t
{
    value,
    nullable_datetime,
};

/**
 * @brief Selects a safe database-generated value for a touched column.
 */
export enum class logical_delete_touch_value : std::uint8_t
{
    current_timestamp,
    current_date,
    current_time,
};

/**
 * @brief Describes one additional assignment made by logical deletion.
 *
 * Field names are validated as SQL identifiers. Values are selected from a
 * closed enumeration, so callers cannot inject arbitrary SQL expressions.
 */
export struct logical_delete_touch_field
{
    std::string field_name;
    logical_delete_touch_value value =
        logical_delete_touch_value::current_timestamp;
};

/**
 * @brief Configures logical-delete predicates and update assignments.
 *
 * `nullable_datetime` treats a null marker as active and writes the database
 * current timestamp when a row is deleted.
 */
export struct logical_delete_config
{
    std::string field_name = "deleted";                   // Field name for logical delete flag
    param_value deleted_value = param_value::from_int(1); // Value when deleted
    param_value not_deleted_value =
        param_value::from_int(0); // Value when not deleted
    logical_delete_mode mode = logical_delete_mode::value;
    std::vector<logical_delete_touch_field> touch_fields;
    bool enabled = true; // Enable/disable logical delete globally
};

// =============================================================================
// logical_delete_interceptor — Intercepts SQL to add logical delete conditions
// =============================================================================

export class logical_delete_interceptor
{
public:
    explicit logical_delete_interceptor(logical_delete_config config = {});

    /// Check if model has logical delete field
    template <Model T> auto has_logical_delete() const -> bool
    {
        if (!config_.enabled)
            return false;

        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, LOGIC_DELETE))
                return true;
            if (field.col.column_name == config_.field_name)
                return true;
        }
        return false;
    }

    /// Get logical delete field name for model
    template <Model T>
    auto get_delete_field() const -> std::optional<std::string>
    {
        if (!config_.enabled)
            return std::nullopt;

        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, LOGIC_DELETE))
                return std::string(field.col.column_name);
            if (field.col.column_name == config_.field_name)
                return std::string(field.col.column_name);
        }
        return std::nullopt;
    }

    /// Inject WHERE condition for SELECT queries
    /// Transforms: SELECT * FROM users WHERE id = 1
    /// To:         SELECT * FROM users WHERE id = 1 AND deleted = 0
    template <Model T>
    auto inject_select_condition(std::string sql) const -> std::string
    {
        auto field = get_delete_field<T>();
        if (!field)
            return sql;
        return inject_select_condition_impl(std::move(sql), *field,
            config_.not_deleted_value, config_.mode);
    }

    /// Transform DELETE to UPDATE for logical delete
    /// Transforms: DELETE FROM users WHERE id = 1
    /// To:         UPDATE users SET deleted = 1 WHERE id = 1
    template <Model T>
    auto transform_delete_to_update(std::string sql) const -> std::string
    {
        auto field = get_delete_field<T>();
        if (!field)
            return sql;

        auto& meta = model_traits<T>::meta();
        return transform_delete_to_update_impl(std::move(sql), meta.table_name,
            *field, config_.deleted_value, config_.mode,
            config_.touch_fields);
    }

    /// Get configuration
    auto config() const noexcept -> const logical_delete_config&;

    /// Set configuration
    void set_config(logical_delete_config config);

    /// Enable/disable logical delete
    void set_enabled(bool enabled);

private:
    static auto inject_select_condition_impl(std::string sql,
        std::string_view field,
        const param_value& not_deleted_value,
        logical_delete_mode mode)
        -> std::string;
    static auto transform_delete_to_update_impl(std::string sql,
        std::string_view table_name,
        std::string_view field,
        const param_value& deleted_value,
        logical_delete_mode mode,
        std::span<const logical_delete_touch_field> touch_fields)
        -> std::string;
    logical_delete_config config_;
};

// =============================================================================
// Global logical delete interceptor instance
// =============================================================================

export auto global_logical_delete_interceptor() -> logical_delete_interceptor&;

// =============================================================================
// Helper macros for defining logical delete fields
// =============================================================================

// Usage in CNETMOD_MODEL:
// CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)

} // namespace cnetmod::orm
