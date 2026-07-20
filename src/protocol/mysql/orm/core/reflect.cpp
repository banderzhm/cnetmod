module cnetmod.protocol.mysql;
import :orm_reflect;

namespace cnetmod::mysql::orm {
auto param_context::from_map(
    std::initializer_list<std::pair<const std::string, param_value>> params)
    -> param_context {
  param_context ctx;
  for (auto &[k, v] : params)
    ctx.values_[k] = expr_value::from_param(v);
  return ctx;
}
auto param_context::resolve(std::string_view name) const -> expr_value {
  return get(name);
}
auto param_context::has(std::string_view name) const -> bool {
  if (values_.contains(std::string(name)))
    return true;
  const auto dot = name.find('.');
  if (dot == std::string_view::npos)
    return false;
  const auto it = nested_.find(std::string(name.substr(0, dot)));
  return it != nested_.end() && it->second.has(name.substr(dot + 1));
}
auto param_context::get(std::string_view name) const -> expr_value {
  if (const auto it = values_.find(std::string(name)); it != values_.end())
    return it->second;
  const auto dot = name.find('.');
  if (dot != std::string_view::npos) {
    if (const auto it = nested_.find(std::string(name.substr(0, dot)));
        it != nested_.end())
      return it->second.get(name.substr(dot + 1));
  }
  return expr_value::make_null();
}
auto param_context::get_param(std::string_view name) const -> param_value {
  return get(name).to_param();
}
void param_context::set(std::string name, expr_value val) {
  values_[std::move(name)] = std::move(val);
}
void param_context::set(std::string name, param_value val) {
  values_[std::move(name)] = expr_value::from_param(val);
}
void param_context::add_nested(std::string name, param_context nested) {
  nested_[std::move(name)] = std::move(nested);
}
void param_context::add_collection(std::string name,
                                   std::vector<param_context> items) {
  collections_[std::move(name)] = std::move(items);
}
auto param_context::get_collection(std::string_view name) const
    -> const std::vector<param_context> * {
  const auto it = collections_.find(std::string(name));
  return it == collections_.end() ? nullptr : &it->second;
}
auto param_context::keys() const -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(values_.size());
  for (const auto &[key, _] : values_)
    result.push_back(key);
  return result;
}
} // namespace cnetmod::mysql::orm
