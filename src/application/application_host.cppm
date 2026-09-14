/**
 * @brief Production application runtime owning networking, services, and telemetry.
 */
export module cnetmod.application.host;

import std;
import cnetmod.application.configuration;
import cnetmod.application.health_registry;
import cnetmod.application.managed_service;
import cnetmod.application.service_registry;
import cnetmod.application.service_lifecycle;
import cnetmod.coro.task;
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
 * The host coordinates service startup, readiness, HTTP draining, supervised
 * tasks, reverse-order shutdown, and telemetry flushing.
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
     * @return The applied changes and whether other changes require a restart.
     */
    [[nodiscard]] auto reload_configuration()
        -> std::expected<configuration_reload_result, std::error_code>;

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
     * @brief Returns the frozen service registry owned by this host.
     */
    [[nodiscard]] auto services() noexcept -> service_registry&;

    /**
     * @brief Returns the cached health registry owned by this host.
     */
    [[nodiscard]] auto health() noexcept -> health_registry&;

    /**
     * @brief Returns the application telemetry composition root.
     */
    [[nodiscard]] auto telemetry() noexcept -> observability::telemetry_hub&;

private:
    class implementation;
    explicit application_host(std::unique_ptr<implementation> implementation);
    std::unique_ptr<implementation> implementation_;

    friend class application_builder;
};

export using route_configurer = std::function<void(http::router&)>;
export using configuration_customizer =
    std::function<void(application_configuration&)>;

/**
 * @brief Builds a validated application composition root.
 *
 * Configuration precedence is defaults, JSON, environment, then explicit
 * customizers. No network listener or managed service is started by build().
 */
export class application_builder
{
public:
    /**
     * @brief Creates a builder with the explicit application name.
     */
    explicit application_builder(std::string name);

    /**
     * @brief Selects the JSON configuration file.
     */
    auto configuration_file(std::filesystem::path path)
        -> application_builder&;

    /**
     * @brief Adds a highest-precedence configuration customizer.
     */
    auto configure(configuration_customizer customizer) -> application_builder&;

    /**
     * @brief Adds business routes to the application HTTP router.
     */
    auto routes(route_configurer configurer) -> application_builder&;

    /**
     * @brief Registers a custom managed service.
     */
    auto service(std::shared_ptr<managed_service> service)
        -> application_builder&;

    /**
     * @brief Enables opt-in auto-configuration for explicitly enabled services.
     */
    auto enable_auto_configuration() noexcept -> application_builder&;

    /**
     * @brief Parses, validates, composes, and freezes the application.
     * @return A ready-to-run host or a validation/registration error.
     */
    [[nodiscard]] auto build()
        -> std::expected<application_host, std::error_code>;

private:
    std::string name_;
    std::optional<std::filesystem::path> configuration_file_;
    std::vector<configuration_customizer> customizers_;
    std::vector<route_configurer> route_configurers_;
    std::vector<std::shared_ptr<managed_service>> services_;
    bool auto_configuration_ = false;
};

} // namespace cnetmod::application
