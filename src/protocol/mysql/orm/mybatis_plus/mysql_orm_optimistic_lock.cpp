module cnetmod.protocol.mysql;

import :orm_optimistic_lock;

namespace cnetmod::mysql::orm {

auto optimistic_lock_interceptor::inject_version_check_impl(
    std::string sql, std::vector<param_value> &params,
    std::string_view version_field, std::int64_t current_version)
    -> std::string {
  auto where_pos = sql.find(" WHERE ");
  if (where_pos == std::string::npos)
    return sql;
  sql.insert(where_pos,
             std::format(", `{}` = `{}` + 1", version_field, version_field));
  where_pos = sql.find(" WHERE ");
  const auto order_pos = sql.find(" ORDER BY", where_pos);
  const auto limit_pos = sql.find(" LIMIT", where_pos);
  const auto insert_pos =
      std::min(order_pos != std::string::npos ? order_pos : sql.size(),
               limit_pos != std::string::npos ? limit_pos : sql.size());
  sql.insert(insert_pos, std::format(" AND `{}` = {{}}", version_field));
  params.push_back(param_value::from_int(current_version));
  return sql;
}

auto optimistic_lock_interceptor::check_update_result(
    std::uint64_t affected_rows) -> bool {
  return affected_rows > 0;
}

optimistic_lock_exception::optimistic_lock_exception(std::string_view msg)
    : std::runtime_error(std::string(msg)) {}

optimistic_lock_exception::optimistic_lock_exception()
    : std::runtime_error("Optimistic lock conflict: version mismatch") {}

auto global_optimistic_lock_interceptor() -> optimistic_lock_interceptor & {
  static optimistic_lock_interceptor instance;
  return instance;
}

} // namespace cnetmod::mysql::orm
