export module cnetmod.protocol.mysql:orm_meta;

import std;
import :types;
import :orm_id_gen;

export namespace cnetmod::mysql::orm {

enum class col_flag : std::uint16_t {
  none = 0,
  primary_key = 1 << 0,
  auto_increment = 1 << 1,
  nullable = 1 << 2,
  version = 1 << 3,
  logic_delete = 1 << 4,
  fill_insert = 1 << 5,
  fill_insert_update = 1 << 6,
  tenant_id = 1 << 7,
};
// Kept constexpr because downstream model declarations form compile-time flag
// constants.
[[nodiscard]] constexpr auto operator|(col_flag left, col_flag right) noexcept
    -> col_flag {
  return static_cast<col_flag>(static_cast<std::uint16_t>(left) |
                               static_cast<std::uint16_t>(right));
}
[[nodiscard]] constexpr auto operator&(col_flag left, col_flag right) noexcept
    -> col_flag {
  return static_cast<col_flag>(static_cast<std::uint16_t>(left) &
                               static_cast<std::uint16_t>(right));
}
[[nodiscard]] constexpr auto has_flag(col_flag flags, col_flag flag) noexcept
    -> bool {
  return static_cast<std::uint16_t>(flags & flag) != 0;
}

struct column_def {
  std::string_view field_name;
  std::string_view column_name;
  column_type type;
  col_flag flags = col_flag::none;
  id_strategy strategy = id_strategy::none;
  [[nodiscard]] auto is_pk() const noexcept -> bool;
  [[nodiscard]] auto is_auto() const noexcept -> bool;
  [[nodiscard]] auto is_nullable() const noexcept -> bool;
  [[nodiscard]] auto is_uuid() const noexcept -> bool;
  [[nodiscard]] auto is_snowflake() const noexcept -> bool;
};

template <class T> using field_setter = void (*)(T &, const field_value &);
template <class T> using field_getter = param_value (*)(const T &);
template <class T> struct field_mapping {
  column_def col;
  field_setter<T> setter;
  field_getter<T> getter;
};
template <class T> struct table_meta {
  std::string_view table_name;
  std::span<const field_mapping<T>> fields;
  [[nodiscard]] auto pk() const noexcept -> const field_mapping<T> * {
    for (auto &field : fields)
      if (field.col.is_pk())
        return &field;
    return nullptr;
  }
  [[nodiscard]] auto find_column(std::string_view name) const noexcept
      -> const field_mapping<T> * {
    for (auto &field : fields)
      if (field.col.column_name == name)
        return &field;
    return nullptr;
  }
  [[nodiscard]] auto insertable_fields() const
      -> std::vector<const field_mapping<T> *> {
    std::vector<const field_mapping<T> *> result;
    for (auto &field : fields)
      if (!field.col.is_auto())
        result.push_back(&field);
    return result;
  }
  [[nodiscard]] auto updatable_fields() const
      -> std::vector<const field_mapping<T> *> {
    std::vector<const field_mapping<T> *> result;
    for (auto &field : fields)
      if (!field.col.is_pk())
        result.push_back(&field);
    return result;
  }
};
template <class T> struct model_traits;
template <class T>
concept Model = requires {
  { model_traits<T>::meta() } -> std::same_as<const table_meta<T> &>;
};
[[nodiscard]] auto sql_type_str(column_type type) noexcept -> std::string_view;

} // namespace cnetmod::mysql::orm

export namespace cnetmod::mysql::orm::detail {
void set_member(std::int64_t &, const field_value &);
void set_member(std::uint64_t &, const field_value &);
void set_member(int &, const field_value &);
void set_member(std::uint32_t &, const field_value &);
void set_member(float &, const field_value &);
void set_member(double &, const field_value &);
void set_member(std::string &, const field_value &);
void set_member(bool &, const field_value &);
void set_member(mysql_date &, const field_value &);
void set_member(mysql_datetime &, const field_value &);
void set_member(mysql_time &, const field_value &);
void set_member(std::optional<std::string> &, const field_value &);
void set_member(std::optional<std::int64_t> &, const field_value &);
void set_member(std::optional<double> &, const field_value &);
void set_member(uuid &, const field_value &);
[[nodiscard]] auto get_member(std::int64_t) -> param_value;
[[nodiscard]] auto get_member(std::uint64_t) -> param_value;
[[nodiscard]] auto get_member(int) -> param_value;
[[nodiscard]] auto get_member(std::uint32_t) -> param_value;
[[nodiscard]] auto get_member(float) -> param_value;
[[nodiscard]] auto get_member(double) -> param_value;
[[nodiscard]] auto get_member(const std::string &) -> param_value;
[[nodiscard]] auto get_member(std::string_view) -> param_value;
[[nodiscard]] auto get_member(bool) -> param_value;
[[nodiscard]] auto get_member(const mysql_date &) -> param_value;
[[nodiscard]] auto get_member(const mysql_datetime &) -> param_value;
[[nodiscard]] auto get_member(const mysql_time &) -> param_value;
[[nodiscard]] auto get_member(const std::optional<std::string> &)
    -> param_value;
[[nodiscard]] auto get_member(const std::optional<std::int64_t> &)
    -> param_value;
[[nodiscard]] auto get_member(const std::optional<double> &) -> param_value;
[[nodiscard]] auto get_member(const uuid &) -> param_value;
template <typename E>
  requires std::is_enum_v<E>
inline void set_member(E &member, const field_value &value) {
  if (value.is_int64())
    member = static_cast<E>(value.get_int64());
  else if (value.is_uint64())
    member = static_cast<E>(value.get_uint64());
}
template <typename E>
  requires std::is_enum_v<E>
[[nodiscard]] inline auto get_member(E value) -> param_value {
  return param_value::from_int(static_cast<std::int64_t>(value));
}
template <typename T>
  requires std::same_as<T, std::time_t> &&
           (!std::same_as<std::time_t, std::int64_t>)
inline void set_member(T &member, const field_value &value) {
  if (value.is_int64())
    member = static_cast<std::time_t>(value.get_int64());
  else if (value.is_uint64())
    member = static_cast<std::time_t>(value.get_uint64());
}
template <typename T>
  requires std::same_as<T, std::time_t> &&
           (!std::same_as<std::time_t, std::int64_t>)
[[nodiscard]] inline auto get_member(T value) -> param_value {
  return param_value::from_int(static_cast<std::int64_t>(value));
}
} // namespace cnetmod::mysql::orm::detail
