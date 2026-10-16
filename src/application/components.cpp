module cnetmod.application.components;

import std;
import cnetmod.application.diagnostics;

namespace cnetmod::application {

namespace {

    /**
     * @brief Internal control flow carrying a resolution failure to build().
     */
    struct resolution_failure
    {
        build_error error;
    };

    [[nodiscard]] auto binding_label(std::string_view display,
        std::string_view name) -> std::string
    {
        if (name.empty())
            return std::string{display};
        return std::format("{} '{}'", display, name);
    }

} // namespace

component_container::component_container(component_fallback fallback,
    std::span<io_context* const> event_loops)
    : event_loops_(event_loops.begin(), event_loops.end()),
      fallback_(std::move(fallback))
{
}

component_container::~component_container()
{
    // Drop lookup references first so the creation list holds the last
    // container-owned reference, then release newest to oldest.
    instances_.clear();
    event_loop_instances_.clear();
    while (!creation_order_.empty())
        creation_order_.pop_back();
}

auto component_container::build(component_collection collection,
    component_fallback fallback, std::span<io_context* const> event_loops)
    -> std::expected<std::unique_ptr<component_container>, build_error>
{
    std::unique_ptr<component_container> container{
        new component_container(std::move(fallback), event_loops)};
    container->registrations_ = std::move(collection.registrations_);
    try
    {
        for (std::size_t position = 0;
             position < container->registrations_.size(); ++position)
        {
            const auto& item = container->registrations_[position];
            if (!item.create)
                return std::unexpected(build_error{
                    .phase = build_phase::resolution,
                    .component = binding_label(item.display, item.name),
                    .message = "registration has no factory"});
            key binding{item.type, item.name};
            if (!container->index_.emplace(std::move(binding), position).second)
                return std::unexpected(build_error{
                    .phase = build_phase::resolution,
                    .component = binding_label(item.display, item.name),
                    .message = "component is registered more than once",
                    .code = std::make_error_code(std::errc::file_exists)});
        }
        for (const auto& item : container->registrations_)
        {
            if (item.scope == component_scope::singleton)
                (void)container->resolve(item.type, item.name, item.display,
                    true);
            else
            {
                if (container->event_loops_.empty())
                    return std::unexpected(build_error{
                        .phase = build_phase::resolution,
                        .component = binding_label(item.display, item.name),
                        .message = "event-loop component requires application event loops",
                        .code = std::make_error_code(std::errc::invalid_argument)});
                for (auto* event_loop : container->event_loops_)
                    (void)container->resolve(item.type, item.name,
                        item.display, true, event_loop);
            }
        }
    }
    catch (const resolution_failure& failure)
    {
        return std::unexpected(failure.error);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(build_error{.phase = build_phase::resolution,
            .message = "out of memory while constructing components",
            .code = std::make_error_code(std::errc::not_enough_memory)});
    }
    return container;
}

auto component_container::lookup(std::type_index type,
    std::string_view name) const -> std::shared_ptr<void>
{
    if (const auto found = instances_.find(key{type, std::string{name}});
        found != instances_.end())
        return found->second;
    if (const auto found = event_loop_instances_.find(
            key{type, std::string{name}});
        found != event_loop_instances_.end())
    {
        auto* current = io_context::current();
        const auto loop = std::ranges::find(event_loops_, current);
        if (loop == event_loops_.end())
            return nullptr;
        return found->second[static_cast<std::size_t>(
            std::distance(event_loops_.begin(), loop))];
    }
    if (fallback_)
        return fallback_(type, name.empty() ? std::string_view{"default"} : name);
    return nullptr;
}

auto component_container::resolve(std::type_index type, std::string_view name,
    std::string_view display, bool required, io_context* event_loop)
    -> std::shared_ptr<void>
{
    key binding{type, std::string{name}};
    if (const auto found = instances_.find(binding); found != instances_.end())
        return found->second;

    const auto registered = index_.find(binding);
    if (registered == index_.end())
    {
        if (fallback_)
        {
            if (auto provided = fallback_(type,
                    name.empty() ? std::string_view{"default"} : name))
                return provided;
        }
        if (!required)
            return nullptr;
        std::string chain;
        for (const auto& frame : resolving_)
            chain += std::format("{} -> ", frame);
        throw resolution_failure{build_error{
            .phase = build_phase::resolution,
            .component = binding_label(display, name),
            .message = resolving_.empty()
                ? std::string{"component is not registered"}
                : std::format("component is not registered (required by {}{})",
                      chain, binding_label(display, name)),
            .code = std::make_error_code(std::errc::no_such_file_or_directory)}};
    }

    auto label = binding_label(display, name);
    if (std::ranges::find(resolving_, label) != resolving_.end())
    {
        std::string chain;
        for (const auto& frame : resolving_)
            chain += std::format("{} -> ", frame);
        throw resolution_failure{build_error{
            .phase = build_phase::resolution,
            .component = label,
            .message = std::format("dependency cycle: {}{}", chain, label),
            .code = std::make_error_code(std::errc::resource_deadlock_would_occur)}};
    }

    auto& item = registrations_[registered->second];
    std::size_t event_loop_index = 0;
    if (item.scope == component_scope::event_loop)
    {
        const auto loop = std::ranges::find(event_loops_, event_loop);
        if (loop == event_loops_.end())
        {
            if (!required)
                return nullptr;
            throw resolution_failure{build_error{
                .phase = build_phase::resolution,
                .component = binding_label(display, name),
                .message = "event-loop component was resolved outside an application event loop",
                .code = std::make_error_code(std::errc::operation_not_permitted)}};
        }
        event_loop_index = static_cast<std::size_t>(
            std::distance(event_loops_.begin(), loop));
        if (const auto found = event_loop_instances_.find(binding);
            found != event_loop_instances_.end() &&
            event_loop_index < found->second.size() &&
            found->second[event_loop_index])
            return found->second[event_loop_index];
    }
    resolving_.push_back(label);
    std::shared_ptr<void> created;
    try
    {
        component_resolver resolver{*this, event_loop};
        created = item.create(resolver,
            item.scope == component_scope::event_loop ? event_loop : nullptr);
    }
    catch (const resolution_failure&)
    {
        throw;
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const std::exception& error)
    {
        throw resolution_failure{build_error{
            .phase = build_phase::resolution,
            .component = label,
            .message = std::format("factory failed: {}", error.what())}};
    }
    catch (...)
    {
        throw resolution_failure{build_error{
            .phase = build_phase::resolution,
            .component = label,
            .message = "factory failed with a non-standard exception"}};
    }
    resolving_.pop_back();
    if (!created)
        throw resolution_failure{build_error{
            .phase = build_phase::resolution,
            .component = label,
            .message = "factory returned null"}};
    if (item.scope == component_scope::singleton)
        instances_.emplace(std::move(binding), created);
    else
    {
        auto& values = event_loop_instances_[binding];
        if (values.empty())
            values.resize(event_loops_.size());
        values[event_loop_index] = created;
    }
    creation_order_.push_back(created);
    return created;
}

auto component_resolver::resolve(std::type_index type, std::string_view name,
    std::string_view display, bool required) -> std::shared_ptr<void>
{
    return container_->resolve(type, name, display, required, event_loop_);
}

} // namespace cnetmod::application
