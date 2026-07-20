module cnetmod.protocol.mysql;
import :orm_type_handler;
namespace cnetmod::mysql::orm {
auto json_type_handler::to_param(const std::string &value) const
    -> param_value {
  return param_value::from_string(value);
}
auto json_type_handler::from_param(const param_value &value) const
    -> std::string {
  return value.kind == param_value::kind_t::string_kind ? value.str_val : "{}";
}
auto uuid_type_handler::is_valid_uuid(std::string_view value) -> bool {
  return value.size() == 36 && value[8] == '-' && value[13] == '-' &&
         value[18] == '-' && value[23] == '-';
}
auto uuid_type_handler::to_param(const std::string &value) const
    -> param_value {
  return is_valid_uuid(value) ? param_value::from_string(value)
                              : param_value::null();
}
auto uuid_type_handler::from_param(const param_value &value) const
    -> std::string {
  return value.kind == param_value::kind_t::string_kind ? value.str_val : "";
}
encrypted_string_handler::encrypted_string_handler(std::string key)
    : key_(std::move(key)) {}
auto encrypted_string_handler::to_param(const std::string &value) const
    -> param_value {
  if (key_.empty())
    return param_value::from_string(value);
  std::string encrypted = value;
  for (std::size_t i = 0; i < encrypted.size(); ++i)
    encrypted[i] ^= key_[i % key_.size()];
  return param_value::from_string(std::move(encrypted));
}
auto encrypted_string_handler::from_param(const param_value &value) const
    -> std::string {
  if (value.kind != param_value::kind_t::string_kind)
    return {};
  if (key_.empty())
    return value.str_val;
  std::string decrypted = value.str_val;
  for (std::size_t i = 0; i < decrypted.size(); ++i)
    decrypted[i] ^= key_[i % key_.size()];
  return decrypted;
}
auto global_type_handler_registry() -> type_handler_registry & {
  static type_handler_registry instance;
  return instance;
}
} // namespace cnetmod::mysql::orm
