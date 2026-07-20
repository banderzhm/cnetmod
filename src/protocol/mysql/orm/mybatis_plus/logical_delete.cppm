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
  std::string field_name = "deleted"; // Field name for logical delete flag
  param_value deleted_value = param_value::from_int(1); // Value when deleted
  param_value not_deleted_value =
      param_value::from_int(0); // Value when not deleted
  bool enabled = true;          // Enable/disable logical delete globally
};

// =============================================================================
// logical_delete_interceptor — Intercepts SQL to add logical delete conditions
// =============================================================================

export class logical_delete_interceptor {
public:
  explicit logical_delete_interceptor(logical_delete_config config = {});

  /// Check if model has logical delete field
  template <Model T> auto has_logical_delete() const -> bool {
    if (!config_.enabled)
      return false;

    auto &meta = model_traits<T>::meta();
    for (auto &field : meta.fields) {
      if (has_flag(field.col.flags, LOGIC_DELETE))
        return true;
      if (field.col.column_name == config_.field_name)
        return true;
    }
    return false;
  }

  /// Get logical delete field name for model
  template <Model T>
  auto get_delete_field() const -> std::optional<std::string> {
    if (!config_.enabled)
      return std::nullopt;

    auto &meta = model_traits<T>::meta();
    for (auto &field : meta.fields) {
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
  auto inject_select_condition(std::string sql) const -> std::string {
    auto field = get_delete_field<T>();
    if (!field)
      return sql;
    return inject_select_condition_impl(std::move(sql), *field,
                                        config_.not_deleted_value);
  }

  /// Transform DELETE to UPDATE for logical delete
  /// Transforms: DELETE FROM users WHERE id = 1
  /// To:         UPDATE users SET deleted = 1 WHERE id = 1
  template <Model T>
  auto transform_delete_to_update(std::string sql) const -> std::string {
    auto field = get_delete_field<T>();
    if (!field)
      return sql;

    auto &meta = model_traits<T>::meta();
    return transform_delete_to_update_impl(std::move(sql), meta.table_name,
                                           *field, config_.deleted_value);
  }

  /// Get configuration
  auto config() const noexcept -> const logical_delete_config &;

  /// Set configuration
  void set_config(logical_delete_config config);

  /// Enable/disable logical delete
  void set_enabled(bool enabled);

private:
  static auto inject_select_condition_impl(std::string sql,
                                           std::string_view field,
                                           const param_value &not_deleted_value)
      -> std::string;
  static auto transform_delete_to_update_impl(std::string sql,
                                              std::string_view table_name,
                                              std::string_view field,
                                              const param_value &deleted_value)
      -> std::string;
  logical_delete_config config_;
};

// =============================================================================
// Global logical delete interceptor instance
// =============================================================================

export auto global_logical_delete_interceptor() -> logical_delete_interceptor &;

// =============================================================================
// Helper macros for defining logical delete fields
// =============================================================================

// Usage in CNETMOD_MODEL:
// CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)

} // namespace cnetmod::mysql::orm
