module cnetmod.application.service_registry;

import std;
import cnetmod.application.recovery_policy;

namespace cnetmod::application {

auto service_registry::manage(std::shared_ptr<managed_service> service)
    -> std::expected<void, std::error_code>
{
    if (auto mutable_registry = ensure_mutable(); !mutable_registry)
        return std::unexpected(mutable_registry.error());
    if (!service)
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    const auto key = service->key();
    if (key.name.empty() || key.instance.empty() || !valid_recovery_policy(service->recovery()))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (managed_.contains(key))
        return std::unexpected(std::make_error_code(std::errc::file_exists));
    try
    {
        managed_.emplace(key, std::move(service));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    return {};
}

auto service_registry::managed_services() const
    -> std::vector<std::shared_ptr<managed_service>>
{
    std::vector<std::shared_ptr<managed_service>> result;
    result.reserve(managed_.size());
    for (const auto& [key, service] : managed_)
    {
        (void)key;
        result.push_back(service);
    }
    return result;
}

auto service_registry::managed(const service_key& key) const noexcept
    -> std::shared_ptr<managed_service>
{
    const auto found = managed_.find(key);
    return found == managed_.end() ? nullptr : found->second;
}

auto service_registry::validate_dependencies() const
    -> std::expected<std::vector<std::vector<service_key>>, std::error_code>
{
    std::unordered_map<service_key, std::size_t, service_key_hash> indegree;
    std::unordered_map<service_key, std::vector<service_key>, service_key_hash>
        dependents;
    for (const auto& [key, service] : managed_)
    {
        indegree.emplace(key, service->dependencies().size());
        for (const auto& dependency : service->dependencies())
        {
            if (!managed_.contains(dependency))
                return std::unexpected(
                    std::make_error_code(std::errc::no_such_file_or_directory));
            dependents[dependency].push_back(key);
        }
    }

    std::vector<std::vector<service_key>> layers;
    std::vector<service_key> ready;
    for (const auto& [key, degree] : indegree)
    {
        if (degree == 0U)
            ready.push_back(key);
    }
    std::size_t visited = 0;
    while (!ready.empty())
    {
        std::ranges::sort(ready, {}, &service_key::canonical_name);
        layers.push_back(ready);
        std::vector<service_key> next;
        for (const auto& key : ready)
        {
            ++visited;
            for (const auto& dependent : dependents[key])
            {
                auto& degree = indegree[dependent];
                if (--degree == 0U)
                    next.push_back(dependent);
            }
        }
        ready = std::move(next);
    }
    if (visited != managed_.size())
        return std::unexpected(
            std::make_error_code(std::errc::too_many_symbolic_link_levels));
    return layers;
}

auto service_registry::size() const noexcept -> std::size_t
{
    return services_.size();
}

auto service_registry::managed_size() const noexcept -> std::size_t
{
    return managed_.size();
}

auto service_registry::frozen() const noexcept -> bool
{
    return frozen_;
}

void service_registry::freeze() noexcept
{
    frozen_ = true;
}

auto service_registry::ensure_mutable() const noexcept
    -> std::expected<void, std::error_code>
{
    if (frozen_)
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));
    return {};
}

} // namespace cnetmod::application
