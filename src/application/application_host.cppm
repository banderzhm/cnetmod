/**
 * @brief Production application runtime owning networking, services, and telemetry.
 */
export module cnetmod.application.host;

import std;
import cnetmod.application.components;
import cnetmod.application.configuration;
import cnetmod.application.diagnostics;
import cnetmod.application.health_registry;
import cnetmod.application.managed_service;
import cnetmod.application.modules;
import cnetmod.application.service_registry;
import cnetmod.application.service_lifecycle;
import cnetmod.application.task_supervisor;
import cnetmod.application.runtime;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.http;

namespace cnetmod::application {

export enum class application_state
{
    built,
    starting,
    running,
    stopping,
    stopped,
    /**
     * Cleanup ended with retained services, HTTP connections, or telemetry work.
     */
    cleanup_failed
};

/**
 * @brief Owns and runs the complete application lifecycle.
 *
 * The host coordinates service startup, module start hooks, readiness, HTTP
 * draining, supervised tasks, module stop hooks, reverse-order shutdown, and
 * telemetry flushing.
 */
export class application_host
{
public:
    application_host(application_host&&) noexcept;
    auto operator=(application_host&&) noexcept -> application_host&;
    ~application_host();

    application_host(const application_host&) = delete;
    auto operator=(const application_host&) -> application_host& = delete;

    /**
     * @brief Runs the application until a stop request or terminal failure.
     * @return Success after graceful shutdown, or the original lifecycle error.
     */
    [[nodiscard]] auto run() -> std::expected<void, std::error_code>;

    /**
     * @brief Retries retained cleanup after run() has returned cleanup_failed.
     * @param timeout Positive budget for this cleanup attempt.
     * @return Success when cleanup settled, independently of the original run error.
     *
     * Call exclusively on the owning thread. Services must cooperate with
     * cancellation; this does not forcibly terminate suspended operations.
     */
    [[nodiscard]] auto retry_cleanup(std::chrono::milliseconds timeout)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Requests idempotent shutdown and may be called from any thread.
     */
    void request_stop() noexcept;

    /**
     * @brief Reloads the configured file and applies runtime-safe changes.
     *
     * Framework settings and application sections are validated completely
     * before anything is published. Runtime-safe options sections are
     * republished; other changed sections are reported as requiring restart.
     */
    [[nodiscard]] auto reload_configuration()
        -> std::expected<configuration_reload_result, configuration_error>;

    /**
     * @brief Returns the current lifecycle state.
     */
    [[nodiscard]] auto state() const noexcept -> application_state;

    /**
     * @brief Returns the most recent structured lifecycle failure.
     */
    [[nodiscard]] auto last_failure() const
        -> std::optional<lifecycle_failure>;

    /**
     * @brief Returns a thread-safe snapshot of the active configuration.
     */
    [[nodiscard]] auto configuration() const
        -> application_configuration;

    /**
     * @brief Returns the frozen managed-service registry owned by this host.
     */
    [[nodiscard]] auto services() noexcept -> service_registry&;

    /**
     * @brief Returns the immutable component container built from modules.
     */
    [[nodiscard]] auto components() const noexcept -> const component_container&;

    /**
     * @brief Returns the cached health registry owned by this host.
     */
    [[nodiscard]] auto health() noexcept -> health_registry&;

    /**
     * @brief Returns the application telemetry composition root.
     */
    [[nodiscard]] auto telemetry() noexcept -> observability::telemetry_hub&;

    /**
     * @brief Returns controlled application execution and telemetry services.
     */
    [[nodiscard]] auto runtime() noexcept -> application_runtime&;

private:
    class implementation;

    explicit application_host(std::unique_ptr<implementation> implementation);

    std::unique_ptr<implementation> implementation_;

    friend class application_builder;
};

export using configuration_customizer =
    std::function<void(application_configuration&)>;

/**
 * @brief Exposes build-time infrastructure to custom managed-service factories.
 *
 * The context is valid only while application_builder::build() is composing
 * the host. It lets infrastructure services bind to the host event loop and
 * telemetry root; business features use modules instead.
 */
export struct application_service_context
{
    io_context& io;
    observability::telemetry_hub& telemetry;
    task_supervisor& supervisor;
    const application_configuration& configuration;
    application_runtime& runtime;
};

export using managed_service_factory = std::function<std::expected<
    std::shared_ptr<managed_service>, std::error_code>(
    application_service_context&)>;

/**
 * @brief Builds a validated application composition root.
 *
 * Configuration precedence is defaults, YAML/JSON, environment, then explicit
 * customizers. build() runs every composition phase (configuration, options,
 * registration, validation, resolution, composition) and reports the first
 * failure as a build_error. No listener or managed service starts in build().
 */
export class application_builder
{
public:
    /**
     * @brief Creates a builder with the explicit application name.
     */
    explicit application_builder(std::string name);

    /**
     * @brief Selects a YAML (.yaml/.yml) or JSON configuration file.
     */
    auto configuration_file(std::filesystem::path path)
        -> application_builder&;

    /**
     * @brief Adds a highest-precedence configuration customizer.
     */
    auto configure(configuration_customizer customizer) -> application_builder&;

    /**
     * @brief Registers an infrastructure managed service.
     */
    auto service(std::shared_ptr<managed_service> service)
        -> application_builder&;

    /**
     * @brief Registers an infrastructure service factory.
     *
     * Factories run in the registration phase with the host event loop,
     * telemetry root, supervisor and immutable configuration.
     */
    auto service_factory(managed_service_factory factory)
        -> application_builder&;

    /**
     * @brief Enables auto-configuration of explicitly enabled services.
     */
    auto enable_auto_configuration() noexcept -> application_builder&;

    /**
     * @brief Adds a module. Modules run in registration order.
     */
    auto add_module(std::shared_ptr<application_module> value)
        -> application_builder&;

    /**
     * @brief Constructs and adds a module of type M.
     */
    template <class M, class... Arguments>
    requires std::derived_from<M, application_module>
    auto add_module(Arguments&&... arguments) -> application_builder&
    {
        return add_module(
            std::make_shared<M>(std::forward<Arguments>(arguments)...));
    }

    /**
     * @brief Parses, validates, composes, and freezes the application.
     */
    [[nodiscard]] auto build() -> std::expected<application_host, build_error>;

private:
    std::string name_;
    std::optional<std::filesystem::path> configuration_file_;
    std::vector<configuration_customizer> customizers_;
    std::vector<std::shared_ptr<managed_service>> services_;
    std::vector<managed_service_factory> service_factories_;
    std::vector<std::shared_ptr<application_module>> modules_;
    bool auto_configuration_ = false;
};

} // namespace cnetmod::application
