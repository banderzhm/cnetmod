module cnetmod.protocol.mysql;

import :orm_logical_delete;

namespace cnetmod::mysql::orm {

logical_delete_interceptor::logical_delete_interceptor(
    logical_delete_config config)
    : config_(std::move(config)) {}

auto logical_delete_interceptor::inject_select_condition_impl(
    std::string sql, std::string_view field,
    const param_value &not_deleted_value) -> std::string {
  const auto value = not_deleted_value.kind == param_value::kind_t::int64_kind
                         ? std::to_string(not_deleted_value.int_val)
                         : "0";
  if (const auto where_pos = sql.find(" WHERE ");
      where_pos != std::string::npos) {
    sql.insert(where_pos + 7, std::format("`{}` = {} AND ", field, value));
    return sql;
  }

  const auto order_pos = sql.find(" ORDER BY");
  const auto group_pos = sql.find(" GROUP BY");
  const auto limit_pos = sql.find(" LIMIT");
  const auto insert_pos =
      std::min({order_pos != std::string::npos ? order_pos : sql.size(),
                group_pos != std::string::npos ? group_pos : sql.size(),
                limit_pos != std::string::npos ? limit_pos : sql.size()});
  sql.insert(insert_pos, std::format(" WHERE `{}` = {}", field, value));
  return sql;
}

auto logical_delete_interceptor::transform_delete_to_update_impl(
    std::string sql, std::string_view table_name, std::string_view field,
    const param_value &deleted_value) -> std::string {
  if (!sql.starts_with("DELETE FROM") && !sql.starts_with("delete from"))
    return sql;
  const auto where_pos = sql.find(" WHERE ");
  const auto where_clause =
      where_pos == std::string::npos ? std::string{} : sql.substr(where_pos);
  const auto value = deleted_value.kind == param_value::kind_t::int64_kind
                         ? std::to_string(deleted_value.int_val)
                         : "1";
  return std::format("UPDATE `{}` SET `{}` = {}{}", table_name, field, value,
                     where_clause);
}

auto logical_delete_interceptor::config() const noexcept
    -> const logical_delete_config & {
  return config_;
}

void logical_delete_interceptor::set_config(logical_delete_config config) {
  config_ = std::move(config);
}

void logical_delete_interceptor::set_enabled(bool enabled) {
  config_.enabled = enabled;
}

auto global_logical_delete_interceptor() -> logical_delete_interceptor & {
  static logical_delete_interceptor instance;
  return instance;
}

} // namespace cnetmod::mysql::orm
