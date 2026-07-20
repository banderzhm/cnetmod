export module cnetmod.protocol.mysql:orm_type_handler;

import std;
import :types;

namespace cnetmod::mysql::orm {
export template <typename T> class type_handler {
public:
  virtual ~type_handler() = default;
  virtual auto to_param(const T &value) const -> param_value = 0;
  virtual auto from_param(const param_value &value) const -> T = 0;
};

export class json_type_handler final : public type_handler<std::string> {
public:
  auto to_param(const std::string &value) const -> param_value override;
  auto from_param(const param_value &value) const -> std::string override;
};

export class uuid_type_handler final : public type_handler<std::string> {
public:
  auto to_param(const std::string &value) const -> param_value override;
  auto from_param(const param_value &value) const -> std::string override;

private:
  static auto is_valid_uuid(std::string_view uuid) -> bool;
};

export class encrypted_string_handler final : public type_handler<std::string> {
public:
  explicit encrypted_string_handler(std::string key);
  auto to_param(const std::string &value) const -> param_value override;
  auto from_param(const param_value &value) const -> std::string override;

private:
  std::string key_;
};

export template <typename T>
class array_type_handler : public type_handler<std::vector<T>> {
public:
  auto to_param(const std::vector<T> &value) const -> param_value override {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
      if (i)
        result += ',';
      if constexpr (std::is_same_v<T, std::string>)
        result += value[i];
      else
        result += std::to_string(value[i]);
    }
    return param_value::from_string(result);
  }
  auto from_param(const param_value &value) const -> std::vector<T> override {
    std::vector<T> result;
    if (value.kind != param_value::kind_t::string_kind)
      return result;
    std::string_view input = value.str_val;
    std::size_t start = 0;
    while (start < input.size()) {
      const auto comma = input.find(',', start);
      const auto token = input.substr(
          start, comma == std::string_view::npos ? comma : comma - start);
      if constexpr (std::is_same_v<T, std::string>)
        result.emplace_back(token);
      else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
        T item{};
        std::from_chars(token.data(), token.data() + token.size(), item);
        result.push_back(item);
      }
      if (comma == std::string_view::npos)
        break;
      start = comma + 1;
    }
    return result;
  }
};

export class type_handler_registry {
private:
  class type_handler_base {
  public:
    virtual ~type_handler_base() = default;
  };
  template <typename T>
  class type_handler_wrapper final : public type_handler_base {
  public:
    explicit type_handler_wrapper(std::unique_ptr<type_handler<T>> handler)
        : handler_(std::move(handler)) {}
    auto get() const -> const type_handler<T> * { return handler_.get(); }

  private:
    std::unique_ptr<type_handler<T>> handler_;
  };

public:
  template <typename T>
  void register_handler(std::unique_ptr<type_handler<T>> handler) {
    handlers_[std::type_index(typeid(T))] =
        std::make_unique<type_handler_wrapper<T>>(std::move(handler));
  }
  template <typename T> auto get_handler() const -> const type_handler<T> * {
    const auto it = handlers_.find(std::type_index(typeid(T)));
    if (it == handlers_.end())
      return nullptr;
    const auto *wrapper =
        dynamic_cast<type_handler_wrapper<T> *>(it->second.get());
    return wrapper ? wrapper->get() : nullptr;
  }

private:
  std::unordered_map<std::type_index, std::unique_ptr<type_handler_base>>
      handlers_;
};
export auto global_type_handler_registry() -> type_handler_registry &;
} // namespace cnetmod::mysql::orm
