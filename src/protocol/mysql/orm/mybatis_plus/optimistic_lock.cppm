export module cnetmod.protocol.mysql:orm_optimistic_lock;

import std;
import :types;
import :orm_meta;
import :client;
import :format_sql;
import cnetmod.coro.task;

namespace cnetmod::mysql::orm {

// =============================================================================
// Optimistic lock field flag
// =============================================================================

export constexpr col_flag VERSION = static_cast<col_flag>(0x20);

// =============================================================================
// optimistic_lock_interceptor — Intercepts UPDATE to add version check
// =============================================================================

export class optimistic_lock_interceptor {
public:
    /// Check if model has version field
    template <Model T>
    auto has_version_field() const -> bool {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, VERSION)) return true;
            if (field.col.column_name == "version" || field.col.column_name == "Version") {
                return true;
            }
        }
        return false;
    }

    /// Get version field name
    template <Model T>
    auto get_version_field() const -> std::optional<std::string> {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, VERSION)) return std::string(field.col.column_name);
            if (field.col.column_name == "version" || field.col.column_name == "Version") {
                return std::string(field.col.column_name);
            }
        }
        return std::nullopt;
    }

    /// Get current version value from entity
    template <Model T>
    auto get_version_value(const T& entity) const -> std::optional<std::int64_t> {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, VERSION) ||
                field.col.column_name == "version" ||
                field.col.column_name == "Version") {
                auto value = field.getter(entity);
                if (value.kind == param_value::kind_t::int64_kind) {
                    return value.int_val;
                } else if (value.kind == param_value::kind_t::uint64_kind) {
                    return static_cast<std::int64_t>(value.uint_val);
                }
            }
        }
        return std::nullopt;
    }

    /// Inject version check into UPDATE SQL
    /// Transforms: UPDATE users SET name = ? WHERE id = ?
    /// To:         UPDATE users SET name = ?, version = version + 1 WHERE id = ? AND version = ?
    template <Model T>
    auto inject_version_check(std::string sql,
                             std::vector<param_value>& params,
                             const T& entity) const -> std::string {
        auto version_field = get_version_field<T>();
        if (!version_field) return sql;

        auto current_version = get_version_value(entity);
        if (!current_version) return sql;

        // Step 1: Add version increment to SET clause
        auto where_pos = sql.find(" WHERE ");
        if (where_pos == std::string::npos) return sql;

        std::string version_set = std::format(", `{}` = `{}` + 1", *version_field, *version_field);
        sql.insert(where_pos, version_set);

        // Step 2: Add version check to WHERE clause
        // Find the end of WHERE clause (before ORDER BY, LIMIT, etc.)
        where_pos = sql.find(" WHERE "); // Re-find after insertion
        auto order_pos = sql.find(" ORDER BY", where_pos);
        auto limit_pos = sql.find(" LIMIT", where_pos);
        auto insert_pos = std::min(
            order_pos != std::string::npos ? order_pos : sql.size(),
            limit_pos != std::string::npos ? limit_pos : sql.size()
        );

        std::string version_check = std::format(" AND `{}` = {{}}", *version_field);
        sql.insert(insert_pos, version_check);
        params.push_back(param_value::from_int(*current_version));

        return sql;
    }

    /// Update entity version after successful UPDATE
    template <Model T>
    auto increment_version(T& entity) const -> void {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, VERSION) ||
                field.col.column_name == "version" ||
                field.col.column_name == "Version") {
                auto value = field.getter(entity);
                if (value.kind == param_value::kind_t::int64_kind) {
                    field.setter(entity, field_value::from_int64(value.int_val + 1));
                } else if (value.kind == param_value::kind_t::uint64_kind) {
                    field.setter(entity, field_value::from_uint64(value.uint_val + 1));
                }
                break;
            }
        }
    }

    /// Check if UPDATE was successful (affected_rows > 0)
    /// If affected_rows == 0, it means version mismatch (optimistic lock conflict)
    static auto check_update_result(std::uint64_t affected_rows) -> bool {
        return affected_rows > 0;
    }
};

// =============================================================================
// optimistic_lock_exception — Thrown when version mismatch occurs
// =============================================================================

export class optimistic_lock_exception : public std::runtime_error {
public:
    explicit optimistic_lock_exception(std::string_view msg)
        : std::runtime_error(std::string(msg)) {}

    optimistic_lock_exception()
        : std::runtime_error("Optimistic lock conflict: version mismatch") {}
};

// =============================================================================
// Global optimistic lock interceptor instance
// =============================================================================

export inline optimistic_lock_interceptor& global_optimistic_lock_interceptor() {
    static optimistic_lock_interceptor instance;
    return instance;
}

// =============================================================================
// Helper: Update with optimistic lock check
// =============================================================================

/// Update entity with optimistic lock check
/// Returns true if successful, false if version mismatch
export template <Model T>
auto update_with_version_check(client& cli, T& entity) -> task<bool> {
    auto& interceptor = global_optimistic_lock_interceptor();

    if (!interceptor.has_version_field<T>()) {
        // No version field, use normal update
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field) co_return false;

        std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);
        std::vector<param_value> params;

        bool first = true;
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, col_flag::primary_key)) continue;
            if (!first) sql += ", ";
            sql += std::format("`{}` = {{}}", field.col.column_name);
            params.push_back(field.getter(entity));
            first = false;
        }

        sql += std::format(" WHERE `{}` = {{}}", pk_field->col.column_name);
        params.push_back(pk_field->getter(entity));

        auto final_sql = format_sql(cli.current_format_opts(), sql, params);
        if (!final_sql) co_return false;

        auto rs = co_await cli.execute(*final_sql);
        co_return rs.ok();
    }

    // Build UPDATE SQL with version check
    auto& meta = model_traits<T>::meta();
    auto* pk_field = meta.pk();
    if (!pk_field) co_return false;

    std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);
    std::vector<param_value> params;

    bool first = true;
    for (auto& field : meta.fields) {
        if (has_flag(field.col.flags, col_flag::primary_key)) continue;
        if (has_flag(field.col.flags, col_flag::auto_increment)) continue;
        if (has_flag(field.col.flags, VERSION)) continue; // Skip version field in SET

        if (!first) sql += ", ";
        sql += std::format("`{}` = {{}}", field.col.column_name);
        params.push_back(field.getter(entity));
        first = false;
    }

    sql += std::format(" WHERE `{}` = {{}}", pk_field->col.column_name);
    params.push_back(pk_field->getter(entity));

    // Inject version check
    sql = interceptor.inject_version_check<T>(sql, params, entity);

    // Execute
    auto final_sql = format_sql(cli.current_format_opts(), sql, params);
    if (!final_sql) co_return false;

    auto rs = co_await cli.execute(*final_sql);
    if (rs.is_err()) co_return false;

    // Check if update was successful
    if (rs.affected_rows == 0) {
        // Version mismatch
        co_return false;
    }

    // Increment version in entity
    interceptor.increment_version(entity);
    co_return true;
}

} // namespace cnetmod::mysql::orm
