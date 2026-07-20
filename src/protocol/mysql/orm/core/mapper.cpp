module cnetmod.protocol.mysql;
import :orm_mapper;

namespace cnetmod::mysql::orm::detail {
auto parse_double_sv(std::string_view input) -> double {
  std::string buffer(input);
  char *end = nullptr;
  const auto value = std::strtod(buffer.c_str(), &end);
  if (end == buffer.c_str())
    throw bad_field_access{};
  return value;
}
auto field_to_double(const field_value &field) -> double {
  if (field.is_null())
    return 0.0;
  if (field.is_double())
    return field.as_double();
  if (field.is_float())
    return static_cast<double>(field.as_float());
  if (field.is_int64())
    return static_cast<double>(field.as_int64());
  if (field.is_uint64())
    return static_cast<double>(field.as_uint64());
  if (field.is_string())
    return parse_double_sv(field.as_string());
  throw bad_field_access{};
}
auto field_to_float(const field_value &field) -> float {
  if (field.is_null())
    return 0.0f;
  if (field.is_float())
    return field.as_float();
  if (field.is_double())
    return static_cast<float>(field.as_double());
  if (field.is_int64())
    return static_cast<float>(field.as_int64());
  if (field.is_uint64())
    return static_cast<float>(field.as_uint64());
  if (field.is_string())
    return static_cast<float>(parse_double_sv(field.as_string()));
  throw bad_field_access{};
}
} // namespace cnetmod::mysql::orm::detail
