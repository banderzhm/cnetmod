module cnetmod.application.auto_configuration;

import std;

namespace cnetmod::application {

auto properties_are_known(const cnetmod::json::document& properties,
    std::initializer_list<std::string_view> allowed) -> bool
{
    if (!properties.is_object())
        return false;
    for (const auto& [key, unused] : properties.get_object())
    {
        (void)unused;
        if (std::ranges::find(allowed, key) == allowed.end())
            return false;
    }
    return true;
}

auto integer_property_in_range(const cnetmod::json::document& properties,
    std::string_view name, std::int64_t minimum, std::int64_t maximum) -> bool
{
    if (!properties.is_object() || minimum > maximum)
        return false;
    const auto* found = cnetmod::json::find(properties, name);
    if (found == nullptr)
        return true;
    if (found->is_uint64())
    {
        const auto value = found->get<std::uint64_t>();
        return maximum >= 0 && value <= static_cast<std::uint64_t>(maximum) &&
            (minimum <= 0 || value >= static_cast<std::uint64_t>(minimum));
    }
    if (!found->is_int64())
        return false;
    const auto value = found->get<std::int64_t>();
    return value >= minimum && value <= maximum;
}

auto pool_size_properties_are_valid(const cnetmod::json::document& properties,
    std::size_t default_minimum, std::size_t default_maximum) -> bool
{
    constexpr auto limit = static_cast<std::int64_t>(std::min<std::uint64_t>(
        std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::int64_t>::max()));
    if (!integer_property_in_range(properties, "minimum_size", 0, limit) ||
        !integer_property_in_range(properties, "maximum_size", 1, limit))
        return false;
    const auto minimum = cnetmod::json::value_or(
        properties, "minimum_size", default_minimum);
    const auto maximum = cnetmod::json::value_or(
        properties, "maximum_size", default_maximum);
    return maximum > 0 && minimum <= maximum;
}

void auto_configuration_registry::add(std::string name,
    service_auto_configurator configurator, integration_loop_mode loop_mode)
{
    if (name.empty() || !configurator)
        throw std::invalid_argument("invalid service auto-configurator");
    if (!configurators_.emplace(std::move(name), registration{
                           .configure = std::move(configurator),
                           .loop_mode = loop_mode})
            .second)
        throw std::logic_error("service auto-configurator already exists");
}

auto auto_configuration_registry::apply(
    const application_configuration& configuration,
    auto_configuration_context& context) const
    -> std::expected<void, std::error_code>
{
    for (const auto& [binding, service] : configuration.services)
    {
        (void)binding;
        if (!service.enabled)
            continue;
        const auto found = configurators_.find(service.name);
        if (found == configurators_.end())
            return std::unexpected(
                std::make_error_code(std::errc::not_supported));
        if (context.event_loops.size() > 1 &&
            found->second.loop_mode ==
                integration_loop_mode::single_loop_only)
            return std::unexpected(
                std::make_error_code(std::errc::not_supported));
        auto result = found->second.configure(service, context);
        if (!result)
            return result;
    }
    return {};
}

auto auto_configuration_registry::contains(std::string_view name) const noexcept
    -> bool
{
    return configurators_.contains(name);
}

auto auto_configuration_registry::supports_multiple_event_loops(
    std::string_view name) const noexcept -> bool
{
    const auto found = configurators_.find(name);
    return found != configurators_.end() &&
        found->second.loop_mode != integration_loop_mode::single_loop_only;
}

} // namespace cnetmod::application
