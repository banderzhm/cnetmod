export module cnetmod.protocol.mysql:orm_reflect;

import std;
import :types;
import :orm_meta;
import :orm_expr;

namespace cnetmod::mysql::orm {

// Runtime parameter bag used by the XML expression engine.  Template model
// adapters remain here because they are instantiated by ORM callers.
export class param_context : public property_resolver {
public:
  param_context() = default;

  static auto from_map(
      std::initializer_list<std::pair<const std::string, param_value>> params)
      -> param_context;

  template <typename Map>
    requires std::ranges::input_range<Map> &&
             std::same_as<
                 std::remove_const_t<
                     typename std::ranges::range_value_t<Map>::first_type>,
                 std::string> &&
             std::same_as<typename std::ranges::range_value_t<Map>::second_type,
                          param_value>
  static auto from_map(Map &&params) -> param_context {
    param_context ctx;
    for (auto &&[k, v] : params)
      ctx.values_[k] = expr_value::from_param(v);
    return ctx;
  }

  template <Model T> static auto from_model(const T &model) -> param_context {
    param_context ctx;
    auto &meta = model_traits<T>::meta();
    for (auto &f : meta.fields) {
      auto pv = f.getter(model);
      ctx.values_[std::string(f.col.field_name)] = expr_value::from_param(pv);
    }
    return ctx;
  }

  auto resolve(std::string_view name) const -> expr_value override;
  auto has(std::string_view name) const -> bool override;
  auto get(std::string_view name) const -> expr_value;
  auto get_param(std::string_view name) const -> param_value;
  void set(std::string name, expr_value val);
  void set(std::string name, param_value val);
  void add_nested(std::string name, param_context nested);
  void add_collection(std::string name, std::vector<param_context> items);
  auto get_collection(std::string_view name) const
      -> const std::vector<param_context> *;
  auto keys() const -> std::vector<std::string>;

private:
  std::unordered_map<std::string, expr_value> values_;
  std::unordered_map<std::string, param_context> nested_;
  std::unordered_map<std::string, std::vector<param_context>> collections_;
};

} // namespace cnetmod::mysql::orm
