// Redis RESP value-model implementation.
module cnetmod.protocol.redis;

import std;
import :value;

namespace cnetmod::redis {
auto to_code(resp3_type t) noexcept -> char {
  switch (t) {
  case resp3_type::blob_error:
    return '!';
  case resp3_type::verbatim_string:
    return '=';
  case resp3_type::blob_string:
    return '$';
  case resp3_type::streamed_string_part:
    return ';';
  case resp3_type::simple_error:
    return '-';
  case resp3_type::number:
    return ':';
  case resp3_type::doublean:
    return ',';
  case resp3_type::boolean:
    return '#';
  case resp3_type::big_number:
    return '(';
  case resp3_type::simple_string:
    return '+';
  case resp3_type::null:
    return '_';
  case resp3_type::push:
    return '>';
  case resp3_type::set:
    return '~';
  case resp3_type::array:
    return '*';
  case resp3_type::attribute:
    return '|';
  case resp3_type::map:
    return '%';
  default:
    return ' ';
  }
}
auto to_type(char c) noexcept -> resp3_type {
  switch (c) {
  case '!':
    return resp3_type::blob_error;
  case '=':
    return resp3_type::verbatim_string;
  case '$':
    return resp3_type::blob_string;
  case ';':
    return resp3_type::streamed_string_part;
  case '-':
    return resp3_type::simple_error;
  case ':':
    return resp3_type::number;
  case ',':
    return resp3_type::doublean;
  case '#':
    return resp3_type::boolean;
  case '(':
    return resp3_type::big_number;
  case '+':
    return resp3_type::simple_string;
  case '_':
    return resp3_type::null;
  case '>':
    return resp3_type::push;
  case '~':
    return resp3_type::set;
  case '*':
    return resp3_type::array;
  case '|':
    return resp3_type::attribute;
  case '%':
    return resp3_type::map;
  default:
    return resp3_type::invalid;
  }
}
auto is_aggregate(resp3_type t) noexcept -> bool {
  return t == resp3_type::array || t == resp3_type::push ||
         t == resp3_type::set || t == resp3_type::map ||
         t == resp3_type::attribute;
}
auto element_multiplicity(resp3_type t) noexcept -> std::size_t {
  return t == resp3_type::map || t == resp3_type::attribute ? 2 : 1;
}
auto type_name(resp3_type t) noexcept -> std::string_view {
  switch (t) {
  case resp3_type::array:
    return "array";
  case resp3_type::push:
    return "push";
  case resp3_type::set:
    return "set";
  case resp3_type::map:
    return "map";
  case resp3_type::attribute:
    return "attribute";
  case resp3_type::simple_string:
    return "simple_string";
  case resp3_type::simple_error:
    return "simple_error";
  case resp3_type::number:
    return "number";
  case resp3_type::doublean:
    return "double";
  case resp3_type::boolean:
    return "boolean";
  case resp3_type::big_number:
    return "big_number";
  case resp3_type::null:
    return "null";
  case resp3_type::blob_error:
    return "blob_error";
  case resp3_type::verbatim_string:
    return "verbatim_string";
  case resp3_type::blob_string:
    return "blob_string";
  case resp3_type::streamed_string_part:
    return "streamed_string_part";
  default:
    return "invalid";
  }
}
auto resp3_node::is_error() const noexcept -> bool {
  return data_type == resp3_type::simple_error ||
         data_type == resp3_type::blob_error;
}
auto resp3_node::is_null() const noexcept -> bool {
  return data_type == resp3_type::null;
}
auto resp3_node::is_aggregate() const noexcept -> bool {
  return redis::is_aggregate(data_type);
}
auto resp3_node::as_integer() const noexcept -> std::int64_t {
  std::int64_t result{};
  std::from_chars(value.data(), value.data() + value.size(), result);
  return result;
}
auto resp3_node::as_double() const noexcept -> double {
  try {
    return std::stod(value);
  } catch (...) {
    return 0.;
  }
}
auto resp3_node::as_bool() const noexcept -> bool {
  return value == "t" || value == "1";
}
auto resp3_node::to_string() const -> std::string {
  switch (data_type) {
  case resp3_type::simple_string:
  case resp3_type::blob_string:
  case resp3_type::verbatim_string:
    return std::format("\"{}\"", value);
  case resp3_type::simple_error:
  case resp3_type::blob_error:
    return std::format("(error) {}", value);
  case resp3_type::number:
  case resp3_type::big_number:
    return std::format("(integer) {}", value);
  case resp3_type::doublean:
    return std::format("(double) {}", value);
  case resp3_type::boolean:
    return std::format("(boolean) {}", value);
  case resp3_type::null:
    return "(nil)";
  case resp3_type::array:
  case resp3_type::set:
    return std::format("({} {})", type_name(data_type), aggregate_size);
  case resp3_type::map:
  case resp3_type::attribute:
    return std::format("({} {} entries)", type_name(data_type), aggregate_size);
  case resp3_type::push:
    return std::format("(push {})", aggregate_size);
  default:
    return "(invalid)";
  }
}
auto operator==(const resp3_node &a, const resp3_node &b) noexcept -> bool {
  return a.data_type == b.data_type && a.aggregate_size == b.aggregate_size &&
         a.depth == b.depth && a.value == b.value;
}
namespace {
class redis_category final : public std::error_category {
public:
  auto name() const noexcept -> const char * override { return "redis"; }
  auto message(int value) const -> std::string override {
    switch (static_cast<redis_errc>(value)) {
    case redis_errc::success:
      return "success";
    case redis_errc::invalid_data_type:
      return "invalid RESP3 data type";
    case redis_errc::not_a_number:
      return "not a number";
    case redis_errc::exceeds_max_nested_depth:
      return "exceeds maximum nested depth";
    case redis_errc::unexpected_bool_value:
      return "unexpected boolean value";
    case redis_errc::empty_field:
      return "empty field";
    case redis_errc::incompatible_size:
      return "incompatible size";
    case redis_errc::not_a_double:
      return "not a double";
    case redis_errc::resp3_simple_error:
      return "RESP3 simple error";
    case redis_errc::resp3_blob_error:
      return "RESP3 blob error";
    case redis_errc::resp3_null:
      return "RESP3 null";
    case redis_errc::not_connected:
      return "not connected";
    case redis_errc::resolve_timeout:
      return "resolve timeout";
    case redis_errc::connect_timeout:
      return "connect timeout";
    case redis_errc::pong_timeout:
      return "pong timeout";
    case redis_errc::ssl_handshake_timeout:
      return "SSL handshake timeout";
    case redis_errc::unknown_error:
      return "unknown redis error";
    default:
      return "unrecognized redis error";
    }
  }
};
auto category() noexcept -> const std::error_category & {
  static redis_category instance;
  return instance;
}
} // namespace
auto make_error_code(redis_errc error) noexcept -> std::error_code {
  return {static_cast<int>(error), category()};
}
} // namespace cnetmod::redis
