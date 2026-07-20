export module cnetmod.protocol.redis:value;

import std;

export namespace cnetmod::redis {
enum class resp3_type {
  array,
  push,
  set,
  map,
  attribute,
  simple_string,
  simple_error,
  number,
  doublean,
  boolean,
  big_number,
  null,
  blob_error,
  verbatim_string,
  blob_string,
  streamed_string_part,
  invalid
};
[[nodiscard]] auto to_code(resp3_type type) noexcept -> char;
[[nodiscard]] auto to_type(char code) noexcept -> resp3_type;
[[nodiscard]] auto is_aggregate(resp3_type type) noexcept -> bool;
[[nodiscard]] auto element_multiplicity(resp3_type type) noexcept
    -> std::size_t;
[[nodiscard]] auto type_name(resp3_type type) noexcept -> std::string_view;
struct resp3_node {
  resp3_type data_type = resp3_type::invalid;
  std::size_t aggregate_size = 0;
  std::size_t depth = 0;
  std::string value;
  [[nodiscard]] auto is_error() const noexcept -> bool;
  [[nodiscard]] auto is_null() const noexcept -> bool;
  [[nodiscard]] auto is_aggregate() const noexcept -> bool;
  [[nodiscard]] auto as_integer() const noexcept -> std::int64_t;
  [[nodiscard]] auto as_double() const noexcept -> double;
  [[nodiscard]] auto as_bool() const noexcept -> bool;
  [[nodiscard]] auto to_string() const -> std::string;
};
[[nodiscard]] auto operator==(const resp3_node &lhs,
                              const resp3_node &rhs) noexcept -> bool;
enum class redis_errc {
  success = 0,
  invalid_data_type,
  not_a_number,
  exceeds_max_nested_depth,
  unexpected_bool_value,
  empty_field,
  incompatible_size,
  not_a_double,
  resp3_simple_error,
  resp3_blob_error,
  resp3_null,
  not_connected,
  resolve_timeout,
  connect_timeout,
  pong_timeout,
  ssl_handshake_timeout,
  unknown_error
};
[[nodiscard]] auto make_error_code(redis_errc error) noexcept
    -> std::error_code;
} // namespace cnetmod::redis

template <>
struct std::is_error_code_enum<cnetmod::redis::redis_errc> : std::true_type {};
