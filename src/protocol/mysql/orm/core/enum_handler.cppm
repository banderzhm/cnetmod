export module cnetmod.protocol.mysql:orm_enum;

import std;
import :types;

namespace cnetmod::mysql::orm {

// =============================================================================
// enum_handler — Base class for enum type handlers
// =============================================================================

export template <typename E>
concept EnumType = std::is_enum_v<E>;

export template <EnumType E>
class enum_handler {
public:
    virtual ~enum_handler() = default;

    /// Convert enum to param_value
    virtual auto to_param(E value) const -> param_value = 0;

    /// Convert param_value to enum
    virtual auto from_param(const param_value& value) const -> E = 0;
};

// =============================================================================
// int_enum_handler — Enum stored as integer
// =============================================================================

export template <EnumType E>
class int_enum_handler : public enum_handler<E> {
public:
    auto to_param(E value) const -> param_value override {
        return param_value::from_int(static_cast<std::int64_t>(value));
    }

    auto from_param(const param_value& value) const -> E override {
        if (value.kind == param_value::kind_t::int64_kind) {
            return static_cast<E>(value.int_val);
        } else if (value.kind == param_value::kind_t::uint64_kind) {
            return static_cast<E>(value.uint_val);
        }
        return static_cast<E>(0);
    }
};

// =============================================================================
// string_enum_handler — Enum stored as string
// =============================================================================

export template <EnumType E>
class string_enum_handler : public enum_handler<E> {
public:
    using enum_map_t = std::unordered_map<E, std::string>;
    using reverse_map_t = std::unordered_map<std::string, E>;

    explicit string_enum_handler(enum_map_t mapping)
        : enum_to_string_(std::move(mapping)) {
        // Build reverse map
        for (auto& [e, s] : enum_to_string_) {
            string_to_enum_[s] = e;
        }
    }

    auto to_param(E value) const -> param_value override {
        auto it = enum_to_string_.find(value);
        if (it != enum_to_string_.end()) {
            return param_value::from_string(it->second);
        }
        return param_value::null();
    }

    auto from_param(const param_value& value) const -> E override {
        if (value.kind == param_value::kind_t::string_kind) {
            auto it = string_to_enum_.find(value.str_val);
            if (it != string_to_enum_.end()) {
                return it->second;
            }
        }
        return static_cast<E>(0);
    }

private:
    enum_map_t enum_to_string_;
    reverse_map_t string_to_enum_;
};

// =============================================================================
// enum_registry — Global registry for enum handlers
// =============================================================================

export class enum_registry {
private:
    // Type-erased base for storing handlers
    class enum_handler_base {
    public:
        virtual ~enum_handler_base() = default;
    };

    // Typed wrapper for enum handlers
    template <EnumType E>
    class enum_handler_wrapper : public enum_handler_base {
    public:
        explicit enum_handler_wrapper(std::unique_ptr<enum_handler<E>> handler)
            : handler_(std::move(handler)) {}

        auto get() const -> const enum_handler<E>* {
            return handler_.get();
        }

    private:
        std::unique_ptr<enum_handler<E>> handler_;
    };

public:
    template <EnumType E>
    void register_int_enum() {
        auto type_id = std::type_index(typeid(E));
        handlers_[type_id] = std::make_unique<enum_handler_wrapper<E>>(
            std::make_unique<int_enum_handler<E>>());
    }

    template <EnumType E>
    void register_string_enum(typename string_enum_handler<E>::enum_map_t mapping) {
        auto type_id = std::type_index(typeid(E));
        handlers_[type_id] = std::make_unique<enum_handler_wrapper<E>>(
            std::make_unique<string_enum_handler<E>>(std::move(mapping)));
    }

    template <EnumType E>
    auto get_handler() const -> const enum_handler<E>* {
        auto type_id = std::type_index(typeid(E));
        auto it = handlers_.find(type_id);
        if (it != handlers_.end()) {
            auto* wrapper = dynamic_cast<enum_handler_wrapper<E>*>(it->second.get());
            if (wrapper) {
                return wrapper->get();
            }
        }
        return nullptr;
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<enum_handler_base>> handlers_;
};

export inline enum_registry& global_enum_registry() {
    static enum_registry instance;
    return instance;
}

} // namespace cnetmod::mysql::orm
