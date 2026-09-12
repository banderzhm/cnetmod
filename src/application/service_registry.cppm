/// Small type-safe registry for application-owned singleton services.
export module cnetmod.application.service_registry;

import std;

namespace cnetmod::application {

export class service_registry
{
public:
    template <class Service, class Implementation = Service, class... Arguments>
    requires(std::same_as<Service, Implementation> ||
        std::derived_from<Implementation, Service>)
    auto emplace(Arguments&&... arguments) -> Service&
    {
        ensure_mutable();
        auto implementation = std::make_shared<Implementation>(
            std::forward<Arguments>(arguments)...);
        std::shared_ptr<Service> service = implementation;
        auto& result = *service;
        services_.insert_or_assign(std::type_index{typeid(Service)},
            std::move(service));
        return result;
    }

    template <class Service>
    void add(std::shared_ptr<Service> service)
    {
        ensure_mutable();
        if (!service)
            throw std::invalid_argument("application service cannot be null");
        services_.insert_or_assign(std::type_index{typeid(Service)},
            std::move(service));
    }

    template <class Service>
    [[nodiscard]] auto find() const noexcept -> Service*
    {
        const auto found = services_.find(std::type_index{typeid(Service)});
        return found == services_.end()
            ? nullptr
            : static_cast<Service*>(found->second.get());
    }

    template <class Service>
    [[nodiscard]] auto require() const -> Service&
    {
        if (auto* service = find<Service>())
            return *service;
        throw std::out_of_range("application service is not registered");
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        return services_.size();
    }

    [[nodiscard]] auto frozen() const noexcept -> bool
    {
        return frozen_;
    }

    void freeze() noexcept
    {
        frozen_ = true;
    }

private:
    void ensure_mutable() const
    {
        if (frozen_)
            throw std::logic_error(
                "application services are immutable after startup");
    }

    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
    bool frozen_ = false;
};

} // namespace cnetmod::application
