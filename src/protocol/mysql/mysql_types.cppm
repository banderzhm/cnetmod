export module cnetmod.protocol.mysql:types;
import std;
import :diagnostics;
export namespace cnetmod::mysql {
enum class field_type : std::uint8_t {
  decimal = 0x00,
  tiny = 0x01,
  short_type = 0x02,
  long_type = 0x03,
  float_type = 0x04,
  double_type = 0x05,
  null_type = 0x06,
  timestamp = 0x07,
  longlong = 0x08,
  int24 = 0x09,
  date = 0x0A,
  time_type = 0x0B,
  datetime = 0x0C,
  year = 0x0D,
  varchar = 0x0F,
  bit = 0x10,
  json = 0xF5,
  newdecimal = 0xF6,
  enum_type = 0xF7,
  set_type = 0xF8,
  tiny_blob = 0xF9,
  medium_blob = 0xFA,
  long_blob = 0xFB,
  blob = 0xFC,
  var_string = 0xFD,
  string = 0xFE,
  geometry = 0xFF
};

enum class field_kind : std::uint8_t {
  null,
  int64,
  uint64,
  string,
  blob,
  float_,
  double_,
  date,
  datetime,
  time
};

auto field_kind_to_str(field_kind) noexcept -> const char *;
using mysql_blob = std::vector<unsigned char>;
using mysql_blob_view = std::span<const unsigned char>;

class bad_field_access : public std::exception {
public:
  const char *what() const noexcept override;
};

enum class column_type {
  tinyint,
  smallint,
  mediumint,
  int_,
  bigint,
  float_,
  double_,
  decimal,
  bit,
  year,
  time,
  date,
  datetime,
  timestamp,
  char_,
  varchar,
  binary,
  varbinary,
  text,
  blob,
  enum_,
  set,
  json,
  geometry,
  unknown
};

namespace column_flags {
inline constexpr std::uint16_t not_null = 1, pri_key = 2, unique_key = 4,
                               multiple_key = 8, is_blob = 16, is_unsigned = 32,
                               zerofill = 64, is_binary = 128, is_enum = 256,
                               auto_increment = 512, is_timestamp = 1024,
                               is_set = 2048, no_default_value = 4096,
                               on_update_now = 8192, part_key = 16384,
                               num = 32768;
}

inline constexpr std::uint16_t binary_collation = 63;
auto compute_column_type(field_type, std::uint16_t, std::uint16_t) noexcept
    -> column_type;
auto column_type_to_str(column_type, bool = false) noexcept -> const char *;

struct mysql_date {
  std::uint16_t year{};
  std::uint8_t month{};
  std::uint8_t day{};
  auto valid() const noexcept -> bool;
  auto to_string() const -> std::string;
  friend constexpr auto operator==(const mysql_date &,
                                   const mysql_date &) noexcept
      -> bool = default;
};

struct mysql_datetime {
  std::uint16_t year{};
  std::uint8_t month{};
  std::uint8_t day{};
  std::uint8_t hour{};
  std::uint8_t minute{};
  std::uint8_t second{};
  std::uint32_t microsecond{};
  auto valid() const noexcept -> bool;
  auto to_string() const -> std::string;
  auto to_date() const noexcept -> mysql_date;
  friend constexpr auto operator==(const mysql_datetime &,
                                   const mysql_datetime &) noexcept
      -> bool = default;
};

struct mysql_time {
  bool negative{};
  std::uint32_t hours{};
  std::uint8_t minutes{};
  std::uint8_t seconds{};
  std::uint32_t microsecond{};
  auto to_microseconds() const noexcept -> std::chrono::microseconds;
  auto to_string() const -> std::string;
  friend constexpr auto operator==(const mysql_time &,
                                   const mysql_time &) noexcept
      -> bool = default;
};

auto compute_field_kind(field_type, std::uint16_t, std::uint16_t) noexcept
    -> field_kind;

struct field_value {
  field_kind kind_ = field_kind::null;
  std::int64_t int_val{};
  std::uint64_t uint_val{};
  float float_val{};
  double double_val{};
  std::string str_val;
  mysql_date date_val;
  mysql_datetime datetime_val;
  mysql_time time_val;
  [[nodiscard]] auto kind() const noexcept -> field_kind;
  [[nodiscard]] auto is_null() const noexcept -> bool;
  [[nodiscard]] auto is_int64() const noexcept -> bool;
  [[nodiscard]] auto is_uint64() const noexcept -> bool;
  [[nodiscard]] auto is_string() const noexcept -> bool;
  [[nodiscard]] auto is_blob() const noexcept -> bool;
  [[nodiscard]] auto is_float() const noexcept -> bool;
  [[nodiscard]] auto is_double() const noexcept -> bool;
  [[nodiscard]] auto is_date() const noexcept -> bool;
  [[nodiscard]] auto is_datetime() const noexcept -> bool;
  [[nodiscard]] auto is_time() const noexcept -> bool;
  auto as_int64() const -> std::int64_t;
  auto as_uint64() const -> std::uint64_t;
  auto as_float() const -> float;
  auto as_double() const -> double;
  auto as_string() const -> std::string_view;
  auto as_blob() const -> mysql_blob_view;
  auto as_date() const -> const mysql_date &;
  auto as_datetime() const -> const mysql_datetime &;
  auto as_time() const -> const mysql_time &;
  auto get_int64() const noexcept -> std::int64_t;
  auto get_uint64() const noexcept -> std::uint64_t;
  auto get_float() const noexcept -> float;
  auto get_double() const noexcept -> double;
  auto get_string() const noexcept -> std::string_view;
  auto get_date() const noexcept -> const mysql_date &;
  auto get_datetime() const noexcept -> const mysql_datetime &;
  auto get_time() const noexcept -> const mysql_time &;
  static auto null() -> field_value;
  static auto from_int64(std::int64_t) -> field_value;
  static auto from_uint64(std::uint64_t) -> field_value;
  static auto from_float(float) -> field_value;
  static auto from_double(double) -> field_value;
  static auto from_string(std::string) -> field_value;
  static auto from_blob(std::string) -> field_value;
  static auto from_date(mysql_date) -> field_value;
  static auto from_datetime(mysql_datetime) -> field_value;
  static auto from_time(mysql_time) -> field_value;
  auto to_string() const -> std::string;

private:
  void chk(field_kind) const;
};

struct column_meta {
  std::string database, table, org_table, name, org_name;
  field_type type = field_type::null_type;
  std::uint16_t flags{};
  std::uint8_t decimals{};
  std::uint16_t charset{};
  std::uint32_t column_length{};
  auto col_type() const noexcept -> column_type;
  auto type_str() const noexcept -> const char *;
  auto is_unsigned() const noexcept -> bool;
  auto is_not_null() const noexcept -> bool;
  auto is_primary_key() const noexcept -> bool;
  auto is_unique_key() const noexcept -> bool;
  auto is_multiple_key() const noexcept -> bool;
  auto is_auto_increment() const noexcept -> bool;
  auto is_zerofill() const noexcept -> bool;
  auto is_binary() const noexcept -> bool;
  auto is_blob_or_text() const noexcept -> bool;
  auto is_enum() const noexcept -> bool;
  auto is_set() const noexcept -> bool;
  auto has_no_default_value() const noexcept -> bool;
  auto is_set_to_now_on_update() const noexcept -> bool;
};

using row = std::vector<field_value>;

struct result_set {
  std::vector<column_meta> columns;
  std::vector<row> rows;
  std::uint64_t affected_rows{};
  std::uint64_t last_insert_id{};
  std::uint16_t warning_count{};
  std::uint16_t status_flags{};
  std::string info, error_msg;
  std::uint16_t error_code{};
  std::string sql_state;
  diagnostics diag;
  auto ok() const noexcept -> bool;
  auto is_err() const noexcept -> bool;
  auto has_rows() const noexcept -> bool;
};

struct statement {
  std::uint32_t id{};
  std::uint16_t num_params{};
  std::uint16_t num_columns{};
  auto valid() const noexcept -> bool;
};

struct param_value {
  enum class kind_t : std::uint8_t {
    null_kind,
    int64_kind,
    uint64_kind,
    double_kind,
    string_kind,
    blob_kind,
    date_kind,
    datetime_kind,
    time_kind
  };

  kind_t kind = kind_t::null_kind;
  std::int64_t int_val{};
  std::uint64_t uint_val{};
  double double_val{};
  std::string str_val;
  mysql_date date_val;
  mysql_datetime datetime_val;
  mysql_time time_val;
  static auto null() -> param_value;
  static auto from_int(std::int64_t) -> param_value;
  static auto from_uint(std::uint64_t) -> param_value;
  static auto from_double(double) -> param_value;
  static auto from_string(std::string) -> param_value;
  static auto from_blob(std::string) -> param_value;
  static auto from_date(mysql_date) -> param_value;
  static auto from_datetime(mysql_datetime) -> param_value;
  static auto from_time(mysql_time) -> param_value;
};

class execution_state {
public:
  enum class state_t : std::uint8_t {
    needs_start,
    reading_rows,
    reading_head,
    complete
  };

  execution_state() noexcept = default;
  auto should_start_op() const noexcept -> bool;
  auto should_read_rows() const noexcept -> bool;
  auto should_read_head() const noexcept -> bool;
  auto is_complete() const noexcept -> bool;
  auto columns() const noexcept -> const std::vector<column_meta> &;
  auto affected_rows() const noexcept -> std::uint64_t;
  auto last_insert_id() const noexcept -> std::uint64_t;
  auto warning_count() const noexcept -> std::uint16_t;
  auto info() const noexcept -> std::string_view;
  auto error_msg() const noexcept -> std::string_view;
  auto error_code() const noexcept -> std::uint16_t;
  void set_state(state_t) noexcept;
  void set_columns(std::vector<column_meta>);
  void set_ok_data(std::uint64_t, std::uint64_t, std::uint16_t, std::uint16_t,
                   std::string);
  void set_error(std::uint16_t, std::string);
  auto status_flags() const noexcept -> std::uint16_t;
  auto has_more_results() const noexcept -> bool;

private:
  state_t state_ = state_t::needs_start;
  std::vector<column_meta> columns_;
  std::uint64_t affected_rows_{}, last_insert_id_{};
  std::uint16_t warning_count_{}, status_flags_{}, error_code_{};
  std::string info_, error_msg_;
};

enum class metadata_mode : std::uint8_t { minimal, full };

struct connect_options {
  std::string host = "*********";
  std::uint16_t port = 3306;
  std::string username = "root", password, database, charset = "utf8mb4";
  ssl_mode ssl = ssl_mode::disable;
  bool tls_verify = true;
  std::string tls_ca_file, tls_cert_file, tls_key_file;
  bool multi_statements{};
  metadata_mode meta_mode = metadata_mode::full;
  std::size_t initial_buffer_size = 8192;
};

enum class isolation_level {
  read_uncommitted,
  read_committed,
  repeatable_read,
  serializable
};

auto isolation_level_to_str(isolation_level) noexcept -> const char *;
} // namespace cnetmod::mysql