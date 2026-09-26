/**
 * @brief Application modules: the unit of composition for business features
 *        and integrations.
 *
 * application_builder drives every module through the same named phases:
 *
 *  1. configure_options  declare typed configuration sections
 *  2. register_components register components (and managed services)
 *  3. (resolution)        the builder constructs every component
 *  4. compose             contribute routes and middleware; all components
 *                         already exist and can be captured by reference
 *  5. on_started          after managed services started, before listening
 *  6. on_stopping         after HTTP drained, before managed services stop
 *
 * Phases 1-4 run inside build(); failures abort the build with a structured
 * diagnostic naming the module. Modules run in registration order; on_stopping
 * runs in reverse order.
 */
export module cnetmod.application.modules;

import std;
import cnetmod.application.components;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.options;
import cnetmod.application.runtime;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod::application {

/**
 * @brief Services available while modules register components.
 */
export struct registration_context
{
    component_collection& components;
    const application_configuration& configuration;
    /// Bound options sections (declared in configure_options). Modules read
    /// them to decide what to register, e.g. one component per configured
    /// provider: `context.options.current<llm_options>("llm")`.
    const options_registry& options;
    /// Registers a lifecycle-managed service owned by this application.
    std::function<void(std::shared_ptr<managed_service>)> manage;
};

/**
 * @brief Services available while modules contribute routes and middleware.
 */
export struct composition_context
{
    /// Business router. Declare route policy through endpoint metadata.
    http::router& routes;
    /// Business middleware, executed in the order appended across modules,
    /// inside framework middleware (recovery, request id, tracing, metrics,
    /// timeout) and outside route handlers.
    std::vector<http::middleware_fn>& middleware;
    const component_container& components;
    application_runtime& runtime;
    const application_configuration& configuration;
};

/**
 * @brief One composable feature of an application.
 */
export class application_module
{
public:
    virtual ~application_module() = default;

    /**
     * @brief Stable module name used in diagnostics.
     */
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /**
     * @brief Declares typed configuration sections owned by this module.
     */
    virtual void configure_options(options_registry& options)
    {
        (void)options;
    }

    /**
     * @brief Registers components and managed services.
     * @return A message describing the failure, if any.
     */
    [[nodiscard]] virtual auto register_components(registration_context& context)
        -> std::expected<void, std::string>
    {
        (void)context;
        return {};
    }

    /**
     * @brief Contributes routes and middleware.
     * @return A message describing the failure, if any.
     */
    [[nodiscard]] virtual auto compose(composition_context& context)
        -> std::expected<void, std::string>
    {
        (void)context;
        return {};
    }

    /**
     * @brief Runs after managed services started and before listeners open.
     *
     * A failure stops startup and rolls back services that already started.
     */
    [[nodiscard]] virtual auto on_started(application_runtime& runtime)
        -> task<std::expected<void, std::error_code>>
    {
        (void)runtime;
        co_return std::expected<void, std::error_code>{};
    }

    /**
     * @brief Runs during shutdown after HTTP drained, before services stop.
     *
     * Must honor the runtime cancellation token; failures are logged.
     */
    [[nodiscard]] virtual auto on_stopping(application_runtime& runtime)
        -> task<void>
    {
        (void)runtime;
        co_return;
    }
};

/**
 * @brief Callbacks for a module defined without a dedicated class.
 */
export struct module_hooks
{
    std::function<void(options_registry&)> configure_options;
    std::function<std::expected<void, std::string>(registration_context&)>
        register_components;
    std::function<std::expected<void, std::string>(composition_context&)> compose;
    std::function<task<std::expected<void, std::error_code>>(application_runtime&)>
        on_started;
    std::function<task<void>(application_runtime&)> on_stopping;
};

/**
 * @brief Module whose phases delegate to callbacks.
 */
export class delegating_module final : public application_module
{
public:
    delegating_module(std::string name, module_hooks hooks)
        : name_(std::move(name)), hooks_(std::move(hooks))
    {
        if (name_.empty())
            throw std::invalid_argument("module name must not be empty");
    }

    [[nodiscard]] auto name() const -> std::string_view override
    {
        return name_;
    }

    void configure_options(options_registry& options) override
    {
        if (hooks_.configure_options)
            hooks_.configure_options(options);
    }

    [[nodiscard]] auto register_components(registration_context& context)
        -> std::expected<void, std::string> override
    {
        if (hooks_.register_components)
            return hooks_.register_components(context);
        return {};
    }

    [[nodiscard]] auto compose(composition_context& context)
        -> std::expected<void, std::string> override
    {
        if (hooks_.compose)
            return hooks_.compose(context);
        return {};
    }

    [[nodiscard]] auto on_started(application_runtime& runtime)
        -> task<std::expected<void, std::error_code>> override
    {
        if (hooks_.on_started)
            co_return co_await hooks_.on_started(runtime);
        co_return std::expected<void, std::error_code>{};
    }

    [[nodiscard]] auto on_stopping(application_runtime& runtime)
        -> task<void> override
    {
        if (hooks_.on_stopping)
            co_await hooks_.on_stopping(runtime);
    }

private:
    std::string name_;
    module_hooks hooks_;
};

/**
 * @brief Creates a module from callbacks.
 */
export [[nodiscard]] inline auto make_module(std::string name, module_hooks hooks)
    -> std::shared_ptr<application_module>
{
    return std::make_shared<delegating_module>(std::move(name), std::move(hooks));
}

} // namespace cnetmod::application
