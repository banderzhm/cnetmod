module cnetmod.protocol.mysql;
import :orm_meta;

namespace cnetmod::mysql::orm {
auto column_def::is_pk() const noexcept -> bool {
  return has_flag(flags, col_flag::primary_key);
}
auto column_def::is_auto() const noexcept -> bool {
  return has_flag(flags, col_flag::auto_increment);
}
auto column_def::is_nullable() const noexcept -> bool {
  return has_flag(flags, col_flag::nullable);
}
auto column_def::is_uuid() const noexcept -> bool {
  return strategy == id_strategy::uuid;
}
auto column_def::is_snowflake() const noexcept -> bool {
  return strategy == id_strategy::snowflake;
}
auto sql_type_str(column_type type) noexcept -> std::string_view {
  switch (type) {
  case column_type::tinyint:
    return "TINYINT";
  case column_type::smallint:
    return "SMALLINT";
  case column_type::mediumint:
    return "MEDIUMINT";
  case column_type::int_:
    return "INT";
  case column_type::bigint:
    return "BIGINT";
  case column_type::float_:
    return "FLOAT";
  case column_type::double_:
    return "DOUBLE";
  case column_type::decimal:
    return "DECIMAL";
  case column_type::bit:
    return "BIT";
  case column_type::year:
    return "YEAR";
  case column_type::time:
    return "TIME";
  case column_type::date:
    return "DATE";
  case column_type::datetime:
    return "DATETIME";
  case column_type::timestamp:
    return "TIMESTAMP";
  case column_type::char_:
    return "CHAR(255)";
  case column_type::varchar:
    return "VARCHAR(255)";
  case column_type::binary:
    return "BINARY(255)";
  case column_type::varbinary:
    return "VARBINARY(255)";
  case column_type::text:
    return "TEXT";
  case column_type::blob:
    return "BLOB";
  case column_type::enum_:
    return "VARCHAR(64)";
  case column_type::set:
    return "VARCHAR(255)";
  case column_type::json:
    return "JSON";
  case column_type::geometry:
    return "GEOMETRY";
  default:
    return "TEXT";
  }
}
} // namespace cnetmod::mysql::orm

namespace cnetmod::mysql::orm::detail {
void set_member(std::int64_t &m, const field_value &v) {
  if (v.is_int64())
    m = v.get_int64();
  else if (v.is_uint64())
    m = static_cast<std::int64_t>(v.get_uint64());
  else if (v.is_string())
    std::from_chars(v.get_string().data(),
                    v.get_string().data() + v.get_string().size(), m);
}
void set_member(std::uint64_t &m, const field_value &v) {
  if (v.is_uint64())
    m = v.get_uint64();
  else if (v.is_int64())
    m = static_cast<std::uint64_t>(v.get_int64());
  else if (v.is_string())
    std::from_chars(v.get_string().data(),
                    v.get_string().data() + v.get_string().size(), m);
}
void set_member(int &m, const field_value &v) {
  std::int64_t n{};
  set_member(n, v);
  m = static_cast<int>(n);
}
void set_member(std::uint32_t &m, const field_value &v) {
  std::uint64_t n{};
  set_member(n, v);
  m = static_cast<std::uint32_t>(n);
}
void set_member(float &m, const field_value &v) {
  if (v.is_float())
    m = v.get_float();
  else if (v.is_double())
    m = static_cast<float>(v.get_double());
}
void set_member(double &m, const field_value &v) {
  if (v.is_double())
    m = v.get_double();
  else if (v.is_float())
    m = v.get_float();
}
void set_member(std::string &m, const field_value &v) {
  if (v.is_string())
    m = v.get_string();
  else if (!v.is_null())
    m = v.to_string();
}
void set_member(bool &m, const field_value &v) {
  if (v.is_int64())
    m = v.get_int64() != 0;
  else if (v.is_uint64())
    m = v.get_uint64() != 0;
}
void set_member(mysql_date &m, const field_value &v) {
  if (v.is_date())
    m = v.get_date();
}
void set_member(mysql_datetime &m, const field_value &v) {
  if (v.is_datetime())
    m = v.get_datetime();
}
void set_member(mysql_time &m, const field_value &v) {
  if (v.is_time())
    m = v.get_time();
}
void set_member(std::optional<std::string> &m, const field_value &v) {
  if (v.is_null())
    m = {};
  else if (v.is_string())
    m = std::string(v.get_string());
  else
    m = v.to_string();
}
void set_member(std::optional<std::int64_t> &m, const field_value &v) {
  if (v.is_null())
    m = {};
  else {
    std::int64_t n{};
    set_member(n, v);
    m = n;
  }
}
void set_member(std::optional<double> &m, const field_value &v) {
  if (v.is_null())
    m = {};
  else {
    double n{};
    set_member(n, v);
    m = n;
  }
}
void set_member(uuid &m, const field_value &v) {
  if (v.is_string())
    if (auto r = uuid::from_string(v.get_string()))
      m = *r;
}
auto get_member(std::int64_t v) -> param_value {
  return param_value::from_int(v);
}
auto get_member(std::uint64_t v) -> param_value {
  return param_value::from_uint(v);
}
auto get_member(int v) -> param_value { return param_value::from_int(v); }
auto get_member(std::uint32_t v) -> param_value {
  return param_value::from_uint(v);
}
auto get_member(float v) -> param_value { return param_value::from_double(v); }
auto get_member(double v) -> param_value { return param_value::from_double(v); }
auto get_member(const std::string &v) -> param_value {
  return param_value::from_string(v);
}
auto get_member(std::string_view v) -> param_value {
  return param_value::from_string(std::string(v));
}
auto get_member(bool v) -> param_value {
  return param_value::from_int(v ? 1 : 0);
}
auto get_member(const mysql_date &v) -> param_value {
  return param_value::from_date(v);
}
auto get_member(const mysql_datetime &v) -> param_value {
  return param_value::from_datetime(v);
}
auto get_member(const mysql_time &v) -> param_value {
  return param_value::from_time(v);
}
auto get_member(const std::optional<std::string> &v) -> param_value {
  return v ? param_value::from_string(*v) : param_value::null();
}
auto get_member(const std::optional<std::int64_t> &v) -> param_value {
  return v ? param_value::from_int(*v) : param_value::null();
}
auto get_member(const std::optional<double> &v) -> param_value {
  return v ? param_value::from_double(*v) : param_value::null();
}
auto get_member(const uuid &v) -> param_value {
  return param_value::from_string(v.to_string());
}
} // namespace cnetmod::mysql::orm::detail
