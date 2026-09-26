module cnetmod.application.options;

import std;
import cnetmod.json;
import cnetmod.application.configuration;
import cnetmod.application.components;
import cnetmod.application.diagnostics;

namespace cnetmod::application {

namespace detail {

    auto reject_unknown_keys(const cnetmod::json::document& provided,
        const cnetmod::json::document& defaults, const std::string& path)
        -> std::expected<void, configuration_error>
    {
        if (!provided.is_object() || !defaults.is_object() || defaults.empty())
            return {};
        for (const auto& [key, value] : provided.get_object())
        {
            const auto* expected = cnetmod::json::find(defaults, key);
            const auto child = std::format("{}.{}", path, key);
            if (expected == nullptr)
                return std::unexpected(
                    configuration_error{.path = child, .message = "unknown key"});
            if (auto nested = reject_unknown_keys(value, *expected, child); !nested)
                return nested;
        }
        return {};
    }

    void overlay(cnetmod::json::document& target,
        const cnetmod::json::document& provided)
    {
        if (!target.is_object() || !provided.is_object())
        {
            target = provided;
            return;
        }
        for (const auto& [key, value] : provided.get_object())
        {
            auto* existing = cnetmod::json::find(target, key);
            if (existing != nullptr && existing->is_object() && value.is_object() &&
                !existing->empty())
                overlay(*existing, value);
            else
                target[key] = value;
        }
    }

} // namespace detail

auto options_registry::bind(const application_configuration& configuration)
    -> std::expected<void, build_error>
{
    for (const auto& [name, unused] : configuration.sections)
    {
        (void)unused;
        const bool claimed = std::ranges::any_of(sections_,
            [&name](const auto& section) { return section->name() == name; });
        if (!claimed)
            return std::unexpected(build_error{
                .phase = build_phase::options,
                .path = name,
                .message = "unknown configuration section; declare it with "
                           "options_registry::section<T>() in a module",
                .code = std::make_error_code(std::errc::invalid_argument)});
    }
    for (const auto& section : sections_)
    {
        const auto found = configuration.sections.find(section->name());
        const auto* provided =
            found == configuration.sections.end() ? nullptr : &found->second;
        auto bound = section->bind(provided);
        if (!bound)
            return std::unexpected(build_error::from(bound.error(), build_phase::options));
    }
    return {};
}

void options_registry::register_components(component_collection& components)
{
    for (const auto& section : sections_)
        section->register_components(components);
}

auto options_registry::reload(const application_configuration& candidate,
    std::span<const std::string> changed)
    -> std::expected<options_reload_outcome, configuration_error>
{
    options_reload_outcome outcome;
    std::vector<std::function<void()>> publications;
    for (const auto& name : changed)
    {
        const auto declared = std::ranges::find_if(sections_,
            [&name](const auto& section) { return section->name() == name; });
        if (declared == sections_.end())
            return std::unexpected(configuration_error{
                .path = name, .message = "unknown configuration section"});
        const auto found = candidate.sections.find(name);
        const auto* provided =
            found == candidate.sections.end() ? nullptr : &found->second;
        // Validate every candidate before publishing any of them.
        auto staged = (*declared)->stage(provided);
        if (!staged)
            return std::unexpected(staged.error());
        if ((*declared)->reload_policy() == options_reload::runtime_safe)
        {
            publications.push_back(std::move(*staged));
            outcome.published.push_back(name);
        }
        else
            outcome.restart_required.push_back(name);
    }
    for (auto& publish : publications)
        publish();
    return outcome;
}

} // namespace cnetmod::application
