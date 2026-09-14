/**
 * @brief Type-safe named services and application-managed infrastructure registry.
 */
export module cnetmod.application.service_registry;

import std;
import cnetmod.application.recovery_policy;
import cnetmod.application.managed_service;

namespace cnetmod::application {

/**
 * @brief Stores typed named bindings and managed lifecycle services.
 *
 * Duplicate registrations fail and freeze() makes the registry immutable.
 */
export class service_registry
{
public:
    /**
     * @brief Constructs and registers a default named service implementation.
     */
    template <class Service, class Implementation = Service, class... Arguments>
    requires(std::same_as<Service, Implementation> ||
        std::derived_from<Implementation, Service>)
    auto emplace(Arguments&&... arguments) -> Service&
    {
        return emplace_named<Service, Implementation>("default",
            std::forward<Arguments>(arguments)...);
    }

    /**
     * @brief Constructs and registers a named service implementation.
     */
    template <class Service, class Implementation = Service, class... Arguments>
    requires(std::same_as<Service, Implementation> ||
        std::derived_from<Implementation, Service>)
    auto emplace_named(std::string instance, Arguments&&... arguments)
        -> Service&
    {
        auto implementation = std::make_shared<Implementation>(
            std::forward<Arguments>(arguments)...);
        std::shared_ptr<Service> service = implementation;
        auto& result = *service;
        add_named<Service>(std::move(instance), std::move(service));
        return result;
    }

    /**
     * @brief Registers an existing service under the default instance name.
     */
    template <class Service>
    void add(std::shared_ptr<Service> service)
    {
        add_named<Service>("default", std::move(service));
    }

    /**
     * @brief Registers an existing service under an explicit instance name.
     */
    template <class Service>
    void add_named(std::string instance, std::shared_ptr<Service> service)
    {
        ensure_mutable();
        if (!service || instance.empty())
            throw std::invalid_argument("invalid application service binding");
        binding_key key{std::type_index{typeid(Service)}, std::move(instance)};
        if (services_.contains(key))
            throw std::logic_error("application service is already registered");
        services_.emplace(std::move(key), std::move(service));
    }

    /**
     * @brief Atomically registers both a typed binding and managed lifecycle entry.
     */
    template <class Service>
    requires std::derived_from<Service, managed_service>
    [[nodiscard]] auto add_managed_named(std::string instance,
        std::shared_ptr<Service> service)
        -> std::expected<void, std::error_code>
    {
        ensure_mutable();
        if (!service || instance.empty())
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        binding_key binding{std::type_index{typeid(Service)}, instance};
        const auto managed_key = service->key();
        if (managed_key.name.empty() || managed_key.instance.empty() ||
            !valid_recovery_policy(service->recovery()))
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        if (services_.contains(binding) || managed_.contains(managed_key))
            return std::unexpected(
                std::make_error_code(std::errc::file_exists));
        managed_.emplace(managed_key, service);
        try
        {
            services_.emplace(std::move(binding), std::move(service));
        }
        catch (...)
        {
            managed_.erase(managed_key);
            throw;
        }
        return {};
    }

    /**
     * @brief Finds a named service without changing registry state.
     */
    template <class Service>
    [[nodiscard]] auto find(std::string_view instance = "default") const noexcept
        -> Service*
    {
        const auto found = services_.find(binding_key{
            std::type_index{typeid(Service)}, std::string{instance}});
        return found == services_.end()
            ? nullptr
            : static_cast<Service*>(found->second.get());
    }

    /**
     * @brief Returns a named service or throws if the binding does not exist.
     */
    template <class Service>
    [[nodiscard]] auto require(std::string_view instance = "default") const
        -> Service&
    {
        if (auto* service = find<Service>(instance))
            return *service;
        throw std::out_of_range("application service is not registered");
    }

    /**
     * @brief Registers an untyped managed service for lifecycle ownership.
     */
    [[nodiscard]] auto manage(std::shared_ptr<managed_service> service)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Returns all managed services as a stable snapshot.
     */
    [[nodiscard]] auto managed_services() const
        -> std::vector<std::shared_ptr<managed_service>>;

    /**
     * @brief Looks up one managed service by its stable key.
     */
    [[nodiscard]] auto managed(const service_key& key) const noexcept
        -> std::shared_ptr<managed_service>;

    /**
     * @brief Validates dependencies and returns parallel topological layers.
     */
    [[nodiscard]] auto validate_dependencies() const
        -> std::expected<std::vector<std::vector<service_key>>, std::error_code>;

    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto managed_size() const noexcept -> std::size_t;
    [[nodiscard]] auto frozen() const noexcept -> bool;
    /**
     * @brief Freezes the registry against all subsequent mutations.
     */
    void freeze() noexcept;

private:
    struct binding_key
    {
        std::type_index service_type{typeid(void)};
        std::string instance;

        auto operator==(const binding_key&) const -> bool = default;
    };

    struct binding_hash
    {
        [[nodiscard]] auto operator()(const binding_key& key) const noexcept
            -> std::size_t
        {
            const auto first = key.service_type.hash_code();
            const auto second = std::hash<std::string>{}(key.instance);
            return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
        }
    };

    void ensure_mutable() const;

    std::unordered_map<binding_key, std::shared_ptr<void>, binding_hash>
        services_;
    std::unordered_map<service_key, std::shared_ptr<managed_service>,
        service_key_hash>
        managed_;
    bool frozen_ = false;
};

} // namespace cnetmod::application
