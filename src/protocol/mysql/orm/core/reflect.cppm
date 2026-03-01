export module cnetmod.protocol.mysql:orm_reflect;

import std;
import :types;
import :orm_meta;
import :orm_expr;

namespace cnetmod::mysql::orm {

// =============================================================================
// param_context — Runtime parameter bag for expression evaluation and SQL generation
// =============================================================================

export class param_context : public property_resolver {
public:
    param_context() = default;

    // Construct from explicit key-value map
    static auto from_map(std::unordered_map<std::string, param_value> params) -> param_context {
        param_context ctx;
        for (auto& [k, v] : params)
            ctx.values_[k] = expr_value::from_param(v);
        return ctx;
    }

    // Construct from a model object (uses model_traits reflection)
    template <Model T>
    static auto from_model(const T& model) -> param_context {
        param_context ctx;
        auto& meta = model_traits<T>::meta();
        for (auto& f : meta.fields) {
            auto pv = f.getter(model);
            ctx.values_[std::string(f.col.field_name)] = expr_value::from_param(pv);
        }
        return ctx;
    }

    // property_resolver interface
    auto resolve(std::string_view name) const -> expr_value override {
        return get(name);
    }

    auto has(std::string_view name) const -> bool override {
        // Check direct values
        if (values_.contains(std::string(name))) return true;
        // Check nested (dot notation)
        auto dot = name.find('.');
        if (dot != std::string_view::npos) {
            auto prefix = name.substr(0, dot);
            auto suffix = name.substr(dot + 1);
            auto it = nested_.find(std::string(prefix));
            if (it != nested_.end()) return it->second.has(suffix);
        }
        return false;
    }

    // Look up a property by name (supports dot notation)
    auto get(std::string_view name) const -> expr_value {
        // Direct lookup
        auto it = values_.find(std::string(name));
        if (it != values_.end()) return it->second;

        // Dot notation: "user.name"
        auto dot = name.find('.');
        if (dot != std::string_view::npos) {
            auto prefix = name.substr(0, dot);
            auto suffix = name.substr(dot + 1);
            auto nested_it = nested_.find(std::string(prefix));
            if (nested_it != nested_.end())
                return nested_it->second.get(suffix);
        }

        return expr_value::make_null();
    }

    // Look up and return as param_value (for SQL binding)
    auto get_param(std::string_view name) const -> param_value {
        return get(name).to_param();
    }

    // Add a value
    void set(std::string name, expr_value val) {
        values_[std::move(name)] = std::move(val);
    }

    void set(std::string name, param_value val) {
        values_[std::move(name)] = expr_value::from_param(val);
    }

    // Add a nested context (for dot notation: "item" -> sub-context)
    void add_nested(std::string name, param_context nested) {
        nested_[std::move(name)] = std::move(nested);
    }

    // foreach collection support
    void add_collection(std::string name, std::vector<param_context> items) {
        collections_[std::move(name)] = std::move(items);
    }

    auto get_collection(std::string_view name) const -> const std::vector<param_context>* {
        auto it = collections_.find(std::string(name));
        if (it != collections_.end()) return &it->second;
        return nullptr;
    }

    // Get all keys (for debugging)
    auto keys() const -> std::vector<std::string> {
        std::vector<std::string> result;
        for (auto& [k, _] : values_) result.push_back(k);
        return result;
    }

private:
    std::unordered_map<std::string, expr_value>              values_;
    std::unordered_map<std::string, param_context>           nested_;
    std::unordered_map<std::string, std::vector<param_context>> collections_;
};

} // namespace cnetmod::mysql::orm
