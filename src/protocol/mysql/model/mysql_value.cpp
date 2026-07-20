// MySQL value-model implementation.
module cnetmod.protocol.mysql;
import :types;

namespace cnetmod::mysql {
auto field_kind_to_str(field_kind k) noexcept -> const char * {
  static constexpr const char *names[] = {
      "null",  "int64",  "uint64", "string",   "blob",
      "float", "double", "date",   "datetime", "time"};
  auto n = std::size_t(k);
  return n < std::size(names) ? names[n] : "<unknown>";
}

const char *bad_field_access::what() const noexcept {
  return "bad_field_access";
}

auto compute_column_type(field_type t, std::uint16_t flags,
                         std::uint16_t charset) noexcept -> column_type {
  switch (t) {
  case field_type::decimal:
  case field_type::newdecimal:
    return column_type::decimal;
  case field_type::tiny:
    return column_type::tinyint;
  case field_type::short_type:
    return column_type::smallint;
  case field_type::int24:
    return column_type::mediumint;
  case field_type::long_type:
    return column_type::int_;
  case field_type::longlong:
    return column_type::bigint;
  case field_type::float_type:
    return column_type::float_;
  case field_type::double_type:
    return column_type::double_;
  case field_type::bit:
    return column_type::bit;
  case field_type::date:
    return column_type::date;
  case field_type::datetime:
    return column_type::datetime;
  case field_type::timestamp:
    return column_type::timestamp;
  case field_type::time_type:
    return column_type::time;
  case field_type::year:
    return column_type::year;
  case field_type::json:
    return column_type::json;
  case field_type::geometry:
    return column_type::geometry;
  case field_type::enum_type:
    return column_type::enum_;
  case field_type::set_type:
    return column_type::set;
  case field_type::string:
    if (flags & column_flags::is_set)
      return column_type::set;
    if (flags & column_flags::is_enum)
      return column_type::enum_;
    return charset == binary_collation ? column_type::binary
                                       : column_type::char_;
  case field_type::varchar:
  case field_type::var_string:
    return charset == binary_collation ? column_type::varbinary
                                       : column_type::varchar;
  case field_type::tiny_blob:
  case field_type::medium_blob:
  case field_type::long_blob:
  case field_type::blob:
    return charset == binary_collation ? column_type::blob : column_type::text;
  default:
    return column_type::unknown;
  }
}

auto column_type_to_str(column_type t, bool u) noexcept -> const char * {
  switch (t) {
  case column_type::tinyint:
    return u ? "TINYINT UNSIGNED" : "TINYINT";
  case column_type::smallint:
    return u ? "SMALLINT UNSIGNED" : "SMALLINT";
  case column_type::mediumint:
    return u ? "MEDIUMINT UNSIGNED" : "MEDIUMINT";
  case column_type::int_:
    return u ? "INT UNSIGNED" : "INT";
  case column_type::bigint:
    return u ? "BIGINT UNSIGNED" : "BIGINT";
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
    return "CHAR";
  case column_type::varchar:
    return "VARCHAR";
  case column_type::binary:
    return "BINARY";
  case column_type::varbinary:
    return "VARBINARY";
  case column_type::text:
    return "TEXT";
  case column_type::blob:
    return "BLOB";
  case column_type::enum_:
    return "ENUM";
  case column_type::set:
    return "SET";
  case column_type::json:
    return "JSON";
  case column_type::geometry:
    return "GEOMETRY";
  default:
    return "<unknown column type>";
  }
}

auto mysql_date::valid() const noexcept -> bool {
  return year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
         day <= 31;
}

auto mysql_date::to_string() const -> std::string {
  return std::format("{:04d}-{:02d}-{:02d}", year, month, day);
}

auto mysql_datetime::valid() const noexcept -> bool {
  return year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
         day <= 31 && hour <= 23 && minute <= 59 && second <= 59 &&
         microsecond <= 999999;
}

auto mysql_datetime::to_string() const -> std::string {
  return microsecond
             ? std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:06d}",
                           year, month, day, hour, minute, second, microsecond)
             : std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", year,
                           month, day, hour, minute, second);
}

auto mysql_datetime::to_date() const noexcept -> mysql_date {
  return {year, month, day};
}

auto mysql_time::to_microseconds() const noexcept -> std::chrono::microseconds {
  auto r = std::chrono::hours(hours) + std::chrono::minutes(minutes) +
           std::chrono::seconds(seconds) +
           std::chrono::microseconds(microsecond);
  return negative ? -r : r;
}

auto mysql_time::to_string() const -> std::string {
  return microsecond
             ? std::format("{}{:02d}:{:02d}:{:02d}.{:06d}", negative ? "-" : "",
                           hours, minutes, seconds, microsecond)
             : std::format("{}{:02d}:{:02d}:{:02d}", negative ? "-" : "", hours,
                           minutes, seconds);
}

auto compute_field_kind(field_type t, std::uint16_t f, std::uint16_t c) noexcept
    -> field_kind {
  switch (t) {
  case field_type::null_type:
    return field_kind::null;
  case field_type::tiny:
  case field_type::short_type:
  case field_type::long_type:
  case field_type::int24:
  case field_type::longlong:
    return f & column_flags::is_unsigned ? field_kind::uint64
                                         : field_kind::int64;
  case field_type::year:
  case field_type::bit:
    return field_kind::uint64;
  case field_type::float_type:
    return field_kind::float_;
  case field_type::double_type:
    return field_kind::double_;
  case field_type::decimal:
  case field_type::newdecimal:
  case field_type::json:
  case field_type::enum_type:
  case field_type::set_type:
    return field_kind::string;
  case field_type::date:
    return field_kind::date;
  case field_type::datetime:
  case field_type::timestamp:
    return field_kind::datetime;
  case field_type::time_type:
    return field_kind::time;
  case field_type::geometry:
    return field_kind::blob;
  case field_type::string:
  case field_type::varchar:
  case field_type::var_string:
  case field_type::tiny_blob:
  case field_type::medium_blob:
  case field_type::long_blob:
  case field_type::blob:
    return c == binary_collation ? field_kind::blob : field_kind::string;
  default:
    return field_kind::string;
  }
}

auto field_value::is_null() const noexcept -> bool {
  return kind_ == field_kind::null;
}
auto field_value::is_int64() const noexcept -> bool {
  return kind_ == field_kind::int64;
}
auto field_value::is_uint64() const noexcept -> bool {
  return kind_ == field_kind::uint64;
}
auto field_value::is_string() const noexcept -> bool {
  return kind_ == field_kind::string;
}
auto field_value::is_blob() const noexcept -> bool {
  return kind_ == field_kind::blob;
}
auto field_value::is_float() const noexcept -> bool {
  return kind_ == field_kind::float_;
}
auto field_value::is_double() const noexcept -> bool {
  return kind_ == field_kind::double_;
}
auto field_value::is_date() const noexcept -> bool {
  return kind_ == field_kind::date;
}
auto field_value::is_datetime() const noexcept -> bool {
  return kind_ == field_kind::datetime;
}
auto field_value::is_time() const noexcept -> bool {
  return kind_ == field_kind::time;
}
auto field_value::kind() const noexcept -> field_kind { return kind_; }
void field_value::chk(field_kind k) const {
  if (kind_ != k)
    throw bad_field_access{};
}

auto field_value::as_int64() const -> std::int64_t {
  chk(field_kind::int64);
  return int_val;
}

auto field_value::as_uint64() const -> std::uint64_t {
  chk(field_kind::uint64);
  return uint_val;
}

auto field_value::as_float() const -> float {
  chk(field_kind::float_);
  return float_val;
}

auto field_value::as_double() const -> double {
  chk(field_kind::double_);
  return double_val;
}

auto field_value::as_string() const -> std::string_view {
  chk(field_kind::string);
  return str_val;
}

auto field_value::as_blob() const -> mysql_blob_view {
  chk(field_kind::blob);
  return {reinterpret_cast<const unsigned char *>(str_val.data()),
          str_val.size()};
}

auto field_value::as_date() const -> const mysql_date & {
  chk(field_kind::date);
  return date_val;
}

auto field_value::as_datetime() const -> const mysql_datetime & {
  chk(field_kind::datetime);
  return datetime_val;
}

auto field_value::as_time() const -> const mysql_time & {
  chk(field_kind::time);
  return time_val;
}

auto field_value::get_int64() const noexcept -> std::int64_t { return int_val; }
auto field_value::get_uint64() const noexcept -> std::uint64_t {
  return uint_val;
}
auto field_value::get_float() const noexcept -> float { return float_val; }
auto field_value::get_double() const noexcept -> double { return double_val; }
auto field_value::get_string() const noexcept -> std::string_view {
  return str_val;
}
auto field_value::get_date() const noexcept -> const mysql_date & {
  return date_val;
}
auto field_value::get_datetime() const noexcept -> const mysql_datetime & {
  return datetime_val;
}
auto field_value::get_time() const noexcept -> const mysql_time & {
  return time_val;
}
auto field_value::null() -> field_value { return {}; }

auto field_value::from_int64(std::int64_t v) -> field_value {
  field_value x;
  x.kind_ = field_kind::int64;
  x.int_val = v;
  return x;
}

auto field_value::from_uint64(std::uint64_t v) -> field_value {
  field_value x;
  x.kind_ = field_kind::uint64;
  x.uint_val = v;
  return x;
}

auto field_value::from_float(float v) -> field_value {
  field_value x;
  x.kind_ = field_kind::float_;
  x.float_val = v;
  return x;
}

auto field_value::from_double(double v) -> field_value {
  field_value x;
  x.kind_ = field_kind::double_;
  x.double_val = v;
  return x;
}

auto field_value::from_string(std::string v) -> field_value {
  field_value x;
  x.kind_ = field_kind::string;
  x.str_val = std::move(v);
  return x;
}

auto field_value::from_blob(std::string v) -> field_value {
  field_value x;
  x.kind_ = field_kind::blob;
  x.str_val = std::move(v);
  return x;
}

auto field_value::from_date(mysql_date v) -> field_value {
  field_value x;
  x.kind_ = field_kind::date;
  x.date_val = v;
  return x;
}

auto field_value::from_datetime(mysql_datetime v) -> field_value {
  field_value x;
  x.kind_ = field_kind::datetime;
  x.datetime_val = v;
  return x;
}

auto field_value::from_time(mysql_time v) -> field_value {
  field_value x;
  x.kind_ = field_kind::time;
  x.time_val = v;
  return x;
}

auto field_value::to_string() const -> std::string {
  switch (kind_) {
  case field_kind::null:
    return "NULL";
  case field_kind::int64:
    return std::to_string(int_val);
  case field_kind::uint64:
    return std::to_string(uint_val);
  case field_kind::float_:
    return std::format("{}", float_val);
  case field_kind::double_:
    return std::format("{}", double_val);
  case field_kind::date:
    return date_val.to_string();
  case field_kind::datetime:
    return datetime_val.to_string();
  case field_kind::time:
    return time_val.to_string();
  default:
    return str_val;
  }
}

auto column_meta::is_unsigned() const noexcept -> bool {
  return (flags & column_flags::is_unsigned) != 0;
}
auto column_meta::is_not_null() const noexcept -> bool {
  return (flags & column_flags::not_null) != 0;
}
auto column_meta::is_primary_key() const noexcept -> bool {
  return (flags & column_flags::pri_key) != 0;
}
auto column_meta::is_unique_key() const noexcept -> bool {
  return (flags & column_flags::unique_key) != 0;
}
auto column_meta::is_multiple_key() const noexcept -> bool {
  return (flags & column_flags::multiple_key) != 0;
}
auto column_meta::is_auto_increment() const noexcept -> bool {
  return (flags & column_flags::auto_increment) != 0;
}
auto column_meta::is_zerofill() const noexcept -> bool {
  return (flags & column_flags::zerofill) != 0;
}
auto column_meta::is_binary() const noexcept -> bool {
  return (flags & column_flags::is_binary) != 0;
}
auto column_meta::is_blob_or_text() const noexcept -> bool {
  return (flags & column_flags::is_blob) != 0;
}
auto column_meta::is_enum() const noexcept -> bool {
  return (flags & column_flags::is_enum) != 0;
}
auto column_meta::is_set() const noexcept -> bool {
  return (flags & column_flags::is_set) != 0;
}
auto column_meta::has_no_default_value() const noexcept -> bool {
  return (flags & column_flags::no_default_value) != 0;
}
auto column_meta::is_set_to_now_on_update() const noexcept -> bool {
  return (flags & column_flags::on_update_now) != 0;
}
auto column_meta::col_type() const noexcept -> column_type {
  return compute_column_type(type, flags, charset);
}
auto column_meta::type_str() const noexcept -> const char * {
  return column_type_to_str(col_type(), is_unsigned());
}
auto result_set::ok() const noexcept -> bool {
  return error_code == 0 && error_msg.empty();
}
auto result_set::is_err() const noexcept -> bool { return !ok(); }
auto result_set::has_rows() const noexcept -> bool { return !rows.empty(); }
auto statement::valid() const noexcept -> bool { return id != 0; }
auto param_value::null() -> param_value { return {}; }

auto param_value::from_int(std::int64_t value) -> param_value {
  param_value result;
  result.kind = kind_t::int64_kind;
  result.int_val = value;
  return result;
}
auto param_value::from_uint(std::uint64_t value) -> param_value {
  param_value result;
  result.kind = kind_t::uint64_kind;
  result.uint_val = value;
  return result;
}
auto param_value::from_double(double value) -> param_value {
  param_value result;
  result.kind = kind_t::double_kind;
  result.double_val = value;
  return result;
}
auto param_value::from_string(std::string value) -> param_value {
  param_value result;
  result.kind = kind_t::string_kind;
  result.str_val = std::move(value);
  return result;
}
auto param_value::from_blob(std::string value) -> param_value {
  param_value result;
  result.kind = kind_t::blob_kind;
  result.str_val = std::move(value);
  return result;
}
auto param_value::from_date(mysql_date value) -> param_value {
  param_value result;
  result.kind = kind_t::date_kind;
  result.date_val = std::move(value);
  return result;
}
auto param_value::from_datetime(mysql_datetime value) -> param_value {
  param_value result;
  result.kind = kind_t::datetime_kind;
  result.datetime_val = std::move(value);
  return result;
}
auto param_value::from_time(mysql_time value) -> param_value {
  param_value result;
  result.kind = kind_t::time_kind;
  result.time_val = std::move(value);
  return result;
}
auto execution_state::should_start_op() const noexcept -> bool {
  return state_ == state_t::needs_start;
}
auto execution_state::should_read_rows() const noexcept -> bool {
  return state_ == state_t::reading_rows;
}
auto execution_state::should_read_head() const noexcept -> bool {
  return state_ == state_t::reading_head;
}
auto execution_state::is_complete() const noexcept -> bool {
  return state_ == state_t::complete;
}
auto execution_state::columns() const noexcept
    -> const std::vector<column_meta> & {
  return columns_;
}
auto execution_state::affected_rows() const noexcept -> std::uint64_t {
  return affected_rows_;
}
auto execution_state::last_insert_id() const noexcept -> std::uint64_t {
  return last_insert_id_;
}
auto execution_state::warning_count() const noexcept -> std::uint16_t {
  return warning_count_;
}
auto execution_state::info() const noexcept -> std::string_view {
  return info_;
}
auto execution_state::error_msg() const noexcept -> std::string_view {
  return error_msg_;
}
auto execution_state::error_code() const noexcept -> std::uint16_t {
  return error_code_;
}
void execution_state::set_state(state_t s) noexcept { state_ = s; }
void execution_state::set_columns(std::vector<column_meta> x) {
  columns_ = std::move(x);
}

void execution_state::set_ok_data(std::uint64_t a, std::uint64_t i,
                                  std::uint16_t w, std::uint16_t s,
                                  std::string info) {
  affected_rows_ = a;
  last_insert_id_ = i;
  warning_count_ = w;
  status_flags_ = s;
  info_ = std::move(info);
}

void execution_state::set_error(std::uint16_t c, std::string e) {
  error_code_ = c;
  error_msg_ = std::move(e);
  state_ = state_t::complete;
}

auto execution_state::status_flags() const noexcept -> std::uint16_t {
  return status_flags_;
}
auto execution_state::has_more_results() const noexcept -> bool {
  return (status_flags_ & 0x0008) != 0;
}

auto isolation_level_to_str(isolation_level l) noexcept -> const char * {
  switch (l) {
  case isolation_level::read_uncommitted:
    return "READ UNCOMMITTED";
  case isolation_level::read_committed:
    return "READ COMMITTED";
  case isolation_level::repeatable_read:
    return "REPEATABLE READ";
  case isolation_level::serializable:
    return "SERIALIZABLE";
  }
  return "REPEATABLE READ";
}
} // namespace cnetmod::mysql
