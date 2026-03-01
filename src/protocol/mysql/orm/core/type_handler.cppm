export module cnetmod.protocol.mysql:orm_type_handler;

import std;
import :types;

namespace cnetmod::mysql::orm {

// =============================================================================
// type_handler — Custom type converter interface
// =============================================================================

export template <typename T>
class type_handler {
public:
    virtual ~type_handler() = default;

    /// Convert C++ type to param_value
    virtual auto to_param(const T& value) const -> param_value = 0;

    /// Convert param_value to C++ type
    virtual auto from_param(const param_value& value) const -> T = 0;
};

// =============================================================================
// Built-in type handlers
// =============================================================================

/// JSON type handler (stores as string)
export class json_type_handler : public type_handler<std::string> {
public:
    auto to_param(const std::string& value) const -> param_value override {
        return param_value::from_string(value);
    }

    auto from_param(const param_value& value) const -> std::string override {
        if (value.kind == param_value::kind_t::string_kind) {
            return value.str_val;
        }
        return "{}";
    }
};

/// UUID type handler
export class uuid_type_handler : public type_handler<std::string> {
public:
    auto to_param(const std::string& value) const -> param_value override {
        // Validate UUID format
        if (is_valid_uuid(value)) {
            return param_value::from_string(value);
        }
        return param_value::null();
    }

    auto from_param(const param_value& value) const -> std::string override {
        if (value.kind == param_value::kind_t::string_kind) {
            return value.str_val;
        }
        return "";
    }

private:
    static auto is_valid_uuid(std::string_view uuid) -> bool {
        // Simple UUID validation (8-4-4-4-12 format)
        if (uuid.size() != 36) return false;
        if (uuid[8] != '-' || uuid[13] != '-' || uuid[18] != '-' || uuid[23] != '-') {
            return false;
        }
        return true;
    }
};

/// Encrypted string type handler
export class encrypted_string_handler : public type_handler<std::string> {
public:
    explicit encrypted_string_handler(std::string key) : key_(std::move(key)) {}

    auto to_param(const std::string& value) const -> param_value override {
        // Simple XOR encryption (replace with real encryption in production)
        std::string encrypted = value;
        for (std::size_t i = 0; i < encrypted.size(); ++i) {
            encrypted[i] ^= key_[i % key_.size()];
        }
        return param_value::from_string(encrypted);
    }

    auto from_param(const param_value& value) const -> std::string override {
        if (value.kind != param_value::kind_t::string_kind) return "";

        // Decrypt (XOR is symmetric)
        std::string decrypted = value.str_val;
        for (std::size_t i = 0; i < decrypted.size(); ++i) {
            decrypted[i] ^= key_[i % key_.size()];
        }
        return decrypted;
    }

private:
    std::string key_;
};

/// Array type handler (stores as comma-separated string)
export template <typename T>
class array_type_handler : public type_handler<std::vector<T>> {
public:
    auto to_param(const std::vector<T>& value) const -> param_value override {
        std::string result;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i > 0) result += ",";
            if constexpr (std::is_same_v<T, std::string>) {
                result += value[i];
            } else {
                result += std::to_string(value[i]);
            }
        }
        return param_value::from_string(result);
    }

    auto from_param(const param_value& value) const -> std::vector<T> override {
        std::vector<T> result;
        if (value.kind != param_value::kind_t::string_kind) return result;

        std::string_view str = value.str_val;
        std::size_t start = 0;

        while (start < str.size()) {
            auto comma = str.find(',', start);
            auto token = str.substr(start, comma == std::string_view::npos ? comma : comma - start);

            if constexpr (std::is_same_v<T, std::string>) {
                result.push_back(std::string(token));
            } else if constexpr (std::is_integral_v<T>) {
                T val;
                std::from_chars(token.data(), token.data() + token.size(), val);
                result.push_back(val);
            } else if constexpr (std::is_floating_point_v<T>) {
                T val;
                std::from_chars(token.data(), token.data() + token.size(), val);
                result.push_back(val);
            }

            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }

        return result;
    }
};

// =============================================================================
// type_handler_registry — Global registry for custom type handlers
// =============================================================================

export class type_handler_registry {
private:
    // Type-erased base for storing handlers
    class type_handler_base {
    public:
        virtual ~type_handler_base() = default;
    };

    // Typed wrapper for type handlers
    template <typename T>
    class type_handler_wrapper : public type_handler_base {
    public:
        explicit type_handler_wrapper(std::unique_ptr<type_handler<T>> handler)
            : handler_(std::move(handler)) {}

        auto get() const -> const type_handler<T>* {
            return handler_.get();
        }

    private:
        std::unique_ptr<type_handler<T>> handler_;
    };

public:
    template <typename T>
    void register_handler(std::unique_ptr<type_handler<T>> handler) {
        auto type_id = std::type_index(typeid(T));
        handlers_[type_id] = std::make_unique<type_handler_wrapper<T>>(std::move(handler));
    }

    template <typename T>
    auto get_handler() const -> const type_handler<T>* {
        auto type_id = std::type_index(typeid(T));
        auto it = handlers_.find(type_id);
        if (it != handlers_.end()) {
            auto* wrapper = dynamic_cast<type_handler_wrapper<T>*>(it->second.get());
            if (wrapper) {
                return wrapper->get();
            }
        }
        return nullptr;
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<type_handler_base>> handlers_;
};

export inline type_handler_registry& global_type_handler_registry() {
    static type_handler_registry instance;
    return instance;
}

} // namespace cnetmod::mysql::orm
