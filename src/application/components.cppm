/**
 * @brief Build-time component container for application composition.
 *
 * Modules register components with factories; application_builder::build()
 * constructs every singleton eagerly, in dependency order, before any listener
 * starts. A missing dependency, a cycle or a throwing factory therefore fails
 * the build with the complete resolution chain instead of surfacing on the
 * first request. The container destroys components in reverse creation order,
 * so a component always outlives everything that borrowed it.
 *
 * Components that are not registered are looked up in the managed service
 * registry, which makes auto-configured infrastructure such as redis_service
 * or mysql_service injectable by type and instance name.
 *
 * After build() the container is immutable; lookups are read-only and safe
 * to perform concurrently.
 */
export module cnetmod.application.components;

import std;
import cnetmod.application.diagnostics;

namespace cnetmod::application {

namespace detail {

    /**
     * @brief Human-readable type name for diagnostics.
     */
    template <class T>
    [[nodiscard]] consteval auto type_name() noexcept -> std::string_view
    {
#if defined(__clang__) || defined(__GNUC__)
        constexpr std::string_view signature = __PRETTY_FUNCTION__;
        constexpr auto start = signature.find("T = ") + 4;
        constexpr auto end = signature.find_first_of(";]", start);
        return signature.substr(start, end - start);
#elif defined(_MSC_VER)
        constexpr std::string_view signature = __FUNCSIG__;
        constexpr auto start = signature.find("type_name<") + 10;
        constexpr auto end = signature.rfind(">(void)");
        return signature.substr(start, end - start);
#else
        return "component";
#endif
    }

    template <class>
    inline constexpr bool is_shared_ptr = false;
    template <class T>
    inline constexpr bool is_shared_ptr<std::shared_ptr<T>> = true;

    template <class>
    inline constexpr bool is_unique_ptr = false;
    template <class T, class Deleter>
    inline constexpr bool is_unique_ptr<std::unique_ptr<T, Deleter>> = true;

    /**
     * @brief Normalizes a factory result into shared ownership of T.
     *
     * Factories may return std::shared_ptr<U>, std::unique_ptr<U>, or a U by
     * value, where U is T or derives from T.
     */
    template <class T, class Result>
    [[nodiscard]] auto to_shared(Result&& result) -> std::shared_ptr<T>
    {
        using R = std::remove_cvref_t<Result>;
        if constexpr (is_shared_ptr<R> || is_unique_ptr<R>)
        {
            std::shared_ptr<T> shared(std::forward<Result>(result));
            if (!shared)
                throw std::invalid_argument("component factory returned null");
            return shared;
        }
        else
        {
            static_assert(std::is_same_v<R, T> || std::is_base_of_v<T, R>,
                "component factory must return T, a type derived from T, or a "
                "smart pointer to one of them");
            return std::make_shared<R>(std::forward<Result>(result));
        }
    }

} // namespace detail

export class component_resolver;
export class component_container;

/**
 * @brief Ordered set of component registrations contributed by modules.
 *
 * The empty name identifies the default binding of a type.
 */
export class component_collection
{
public:
    /**
     * @brief Registers a default-named singleton built by a factory.
     *
     * The factory receives a component_resolver and may resolve other
     * components. It returns T (by value), std::unique_ptr, or std::shared_ptr
     * to T or a derived type.
     */
    template <class T, class Factory>
    requires std::invocable<Factory&, component_resolver&>
    auto singleton(Factory factory) -> component_collection&
    {
        return singleton<T>(std::string{}, std::move(factory));
    }

    /**
     * @brief Registers a named singleton built by a factory.
     */
    template <class T, class Factory>
    requires std::invocable<Factory&, component_resolver&>
    auto singleton(std::string name, Factory factory) -> component_collection&
    {
        registrations_.push_back(registration{
            .type = std::type_index{typeid(T)},
            .name = std::move(name),
            .display = detail::type_name<T>(),
            .create = [factory = std::move(factory)](component_resolver& resolver) mutable
                -> std::shared_ptr<void>
            {
                return detail::to_shared<T>(std::invoke(factory, resolver));
            },
        });
        return *this;
    }

    /**
     * @brief Registers an already constructed, shared component.
     */
    template <class T>
    auto instance(std::shared_ptr<T> value, std::string name = {})
        -> component_collection&
    {
        if (!value)
            throw std::invalid_argument("component instance must not be null");
        registrations_.push_back(registration{
            .type = std::type_index{typeid(T)},
            .name = std::move(name),
            .display = detail::type_name<T>(),
            .create = [value = std::move(value)](component_resolver&)
                -> std::shared_ptr<void> { return value; },
        });
        return *this;
    }

    /**
     * @brief Registers a component owned elsewhere that outlives the container.
     */
    template <class T>
    auto borrow(T& value, std::string name = {}) -> component_collection&
    {
        return instance(std::shared_ptr<T>(std::shared_ptr<T>{}, &value),
            std::move(name));
    }

    /**
     * @brief Exposes an implementation registration under an interface type.
     *
     * Resolving Interface yields the same object as resolving Implementation.
     */
    template <class Interface, class Implementation>
    requires std::derived_from<Implementation, Interface>
    auto alias(std::string name = {}, std::string implementation_name = {})
        -> component_collection&;

    /**
     * @brief Reports whether a registration exists for the binding.
     */
    template <class T>
    [[nodiscard]] auto contains(std::string_view name = {}) const noexcept -> bool
    {
        const std::type_index type{typeid(T)};
        return std::ranges::any_of(registrations_,
            [type, name](const registration& item)
            {
                return item.type == type && item.name == name;
            });
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        return registrations_.size();
    }

private:
    struct registration
    {
        std::type_index type;
        std::string name;
        std::string_view display;
        std::function<std::shared_ptr<void>(component_resolver&)> create;
    };

    std::vector<registration> registrations_;

    friend class component_container;
};

/**
 * @brief Resolution facade handed to component factories during build.
 *
 * Resolution failures abort the build with the chain of components being
 * constructed. The resolver must not be retained beyond the factory call.
 */
export class component_resolver
{
public:
    component_resolver(const component_resolver&) = delete;
    auto operator=(const component_resolver&) -> component_resolver& = delete;

    /**
     * @brief Resolves a required component.
     */
    template <class T>
    [[nodiscard]] auto get(std::string_view name = {}) -> T&
    {
        return *shared<T>(name);
    }

    /**
     * @brief Resolves a required component with shared ownership.
     */
    template <class T>
    [[nodiscard]] auto shared(std::string_view name = {}) -> std::shared_ptr<T>
    {
        return std::static_pointer_cast<T>(resolve(std::type_index{typeid(T)},
            name, detail::type_name<T>(), true));
    }

    /**
     * @brief Resolves an optional component; null when neither registered nor
     *        provided by the managed service registry.
     */
    template <class T>
    [[nodiscard]] auto find(std::string_view name = {}) -> T*
    {
        return static_cast<T*>(resolve(std::type_index{typeid(T)}, name,
            detail::type_name<T>(), false)
                .get());
    }

private:
    explicit component_resolver(component_container& container) noexcept
        : container_(&container)
    {
    }

    [[nodiscard]] auto resolve(std::type_index type, std::string_view name,
        std::string_view display, bool required) -> std::shared_ptr<void>;

    component_container* container_;

    friend class component_container;
};

template <class Interface, class Implementation>
requires std::derived_from<Implementation, Interface>
auto component_collection::alias(std::string name,
    std::string implementation_name) -> component_collection&
{
    registrations_.push_back(registration{
        .type = std::type_index{typeid(Interface)},
        .name = std::move(name),
        .display = detail::type_name<Interface>(),
        .create = [implementation_name = std::move(implementation_name)](
                      component_resolver& resolver) -> std::shared_ptr<void>
        {
            std::shared_ptr<Interface> resolved =
                resolver.shared<Implementation>(implementation_name);
            return resolved;
        },
    });
    return *this;
}

/**
 * @brief Fallback lookup for components owned outside the container.
 *
 * Receives the requested type and binding name ("" maps to "default").
 */
export using component_fallback = std::function<std::shared_ptr<void>(
    std::type_index, std::string_view)>;

/**
 * @brief Immutable container of constructed components.
 */
export class component_container
{
public:
    /**
     * @brief Constructs every registration eagerly in dependency order.
     */
    [[nodiscard]] static auto build(component_collection collection,
        component_fallback fallback = {})
        -> std::expected<std::unique_ptr<component_container>, build_error>;

    component_container(const component_container&) = delete;
    auto operator=(const component_container&) -> component_container& = delete;
    ~component_container();

    /**
     * @brief Returns a component; throws std::out_of_range when absent.
     */
    template <class T>
    [[nodiscard]] auto get(std::string_view name = {}) const -> T&
    {
        if (auto* found = find<T>(name))
            return *found;
        throw std::out_of_range(std::format("component {} '{}' is not registered",
            detail::type_name<T>(), name));
    }

    /**
     * @brief Returns a component or null when absent.
     */
    template <class T>
    [[nodiscard]] auto find(std::string_view name = {}) const -> T*
    {
        return static_cast<T*>(lookup(std::type_index{typeid(T)}, name).get());
    }

    /**
     * @brief Returns shared ownership of a component or null when absent.
     */
    template <class T>
    [[nodiscard]] auto shared(std::string_view name = {}) const
        -> std::shared_ptr<T>
    {
        return std::static_pointer_cast<T>(
            lookup(std::type_index{typeid(T)}, name));
    }

    /**
     * @brief Number of components constructed by this container.
     */
    [[nodiscard]] auto constructed() const noexcept -> std::size_t
    {
        return creation_order_.size();
    }

private:
    struct key
    {
        std::type_index type;
        std::string name;
        auto operator==(const key&) const -> bool = default;
    };

    struct key_hash
    {
        [[nodiscard]] auto operator()(const key& value) const noexcept
            -> std::size_t
        {
            const auto first = value.type.hash_code();
            const auto second = std::hash<std::string>{}(value.name);
            return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
        }
    };

    explicit component_container(component_fallback fallback);

    [[nodiscard]] auto lookup(std::type_index type, std::string_view name) const
        -> std::shared_ptr<void>;
    [[nodiscard]] auto resolve(std::type_index type, std::string_view name,
        std::string_view display, bool required) -> std::shared_ptr<void>;

    std::vector<component_collection::registration> registrations_;
    std::unordered_map<key, std::size_t, key_hash> index_;
    std::unordered_map<key, std::shared_ptr<void>, key_hash> instances_;
    std::vector<std::shared_ptr<void>> creation_order_;
    std::vector<std::string> resolving_;
    component_fallback fallback_;

    friend class component_resolver;
};

} // namespace cnetmod::application
