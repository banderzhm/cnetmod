module cnetmod.protocol.mysql;

import :orm_multi_tenant;

namespace cnetmod::mysql::orm {

thread_local std::optional<std::int64_t> tenant_context::current_tenant_id_;

void tenant_context::set_tenant_id(std::int64_t tenant_id) {
  current_tenant_id_ = tenant_id;
}

auto tenant_context::get_tenant_id() -> std::optional<std::int64_t> {
  return current_tenant_id_;
}

void tenant_context::clear() { current_tenant_id_ = std::nullopt; }

multi_tenant_interceptor::multi_tenant_interceptor(std::string tenant_field)
    : tenant_field_(std::move(tenant_field)) {}

auto multi_tenant_interceptor::inject_tenant_condition_impl(
    std::string sql, std::vector<param_value> &params,
    std::string_view tenant_field, std::int64_t tenant_id) -> std::string {
  if (const auto where_pos = sql.find(" WHERE ");
      where_pos != std::string::npos) {
    sql.insert(where_pos + 7, std::format("`{}` = {{}} AND ", tenant_field));
    params.insert(params.begin(), param_value::from_int(tenant_id));
    return sql;
  }

  const auto order_pos = sql.find(" ORDER BY");
  const auto group_pos = sql.find(" GROUP BY");
  const auto limit_pos = sql.find(" LIMIT");
  const auto having_pos = sql.find(" HAVING");
  const auto insert_pos =
      std::min({order_pos != std::string::npos ? order_pos : sql.size(),
                group_pos != std::string::npos ? group_pos : sql.size(),
                limit_pos != std::string::npos ? limit_pos : sql.size(),
                having_pos != std::string::npos ? having_pos : sql.size()});
  sql.insert(insert_pos, std::format(" WHERE `{}` = {{}}", tenant_field));
  params.push_back(param_value::from_int(tenant_id));
  return sql;
}

auto multi_tenant_interceptor::inject_tenant_insert_impl(
    std::string sql, std::vector<param_value> &params,
    std::string_view tenant_field, std::int64_t tenant_id) -> std::string {
  const auto values_pos = sql.find(" VALUES ");
  if (values_pos == std::string::npos)
    return sql;
  const auto col_start = sql.find('(');
  const auto col_end = sql.find(')', col_start);
  if (col_start == std::string::npos || col_end == std::string::npos)
    return sql;
  sql.insert(col_end, std::format(", `{}`", tenant_field));
  const auto val_start = sql.find('(', values_pos);
  const auto val_end = sql.find(')', val_start);
  if (val_start != std::string::npos && val_end != std::string::npos) {
    sql.insert(val_end, ", {}");
    params.push_back(param_value::from_int(tenant_id));
  }
  return sql;
}

void multi_tenant_interceptor::set_tenant_field(std::string field) {
  tenant_field_ = std::move(field);
}

auto multi_tenant_interceptor::tenant_field() const noexcept
    -> const std::string & {
  return tenant_field_;
}

auto global_multi_tenant_interceptor() -> multi_tenant_interceptor & {
  static multi_tenant_interceptor instance;
  return instance;
}

tenant_guard::tenant_guard(std::int64_t tenant_id) {
  tenant_context::set_tenant_id(tenant_id);
}

tenant_guard::~tenant_guard() { tenant_context::clear(); }

} // namespace cnetmod::mysql::orm
