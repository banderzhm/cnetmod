/**
 * @brief Explicit, build-feature-aware infrastructure auto-configuration.
 */
export module cnetmod.application.auto_configuration;

import std;
import cnetmod.json;
import cnetmod.application.configuration;
import cnetmod.application.service_registry;
import cnetmod.application.task_supervisor;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.http;

namespace cnetmod::application {

export struct auto_configuration_context
{
    /// Control-plane loop used for lifecycle and management operations.
    io_context& io;
    /// Business loops. In single-loop mode this contains only `io`.
    std::span<io_context* const> event_loops;
    observability::telemetry_hub& telemetry;
    task_supervisor& supervisor;
    service_registry& services;
    http::router& routes;
};

export using service_auto_configurator = std::function<
    std::expected<void, std::error_code>(const configured_service&,
    auto_configuration_context&)>;

/** Declares how an integration participates in a multi-loop application. */
export enum class integration_loop_mode
{
    /** Rejected when more than one application event loop is configured. */
    single_loop_only,
    /** One independent transport/pool is created for every event loop. */
    loop_local,
    /** One owner loop is used and continuations are marshalled explicitly. */
    owned,
    /** The integration is intrinsically safe for concurrent loop access. */
    shared,
};

/**
 * @brief Validates that a service configuration contains only allowed keys.
 */
export [[nodiscard]] auto properties_are_known(const cnetmod::json::document& properties,
    std::initializer_list<std::string_view> allowed) -> bool;

/**
 * @brief Checks an optional integer property without narrowing or coercion.
 * Missing properties retain their defaults; present values must be integers
 * within the inclusive bounds. Booleans and floating-point values are rejected.
 */
export [[nodiscard]] auto integer_property_in_range(const cnetmod::json::document& properties,
    std::string_view name, std::int64_t minimum, std::int64_t maximum) -> bool;

/**
 * @brief Validates pool capacity properties before conversion or allocation.
 * Applies defaults to omitted bounds and requires zero or more initial
 * connections, a positive maximum, and minimum no greater than maximum.
 */
export [[nodiscard]] auto pool_size_properties_are_valid(const cnetmod::json::document& properties,
    std::size_t default_minimum, std::size_t default_maximum) -> bool;

export class auto_configuration_registry
{
public:
    /**
     * @brief Registers one configurator by integration type.
     */
    void add(std::string name, service_auto_configurator configurator,
        integration_loop_mode loop_mode =
            integration_loop_mode::single_loop_only);

    /**
     * @brief Applies configurators for all explicitly enabled services.
     */
    [[nodiscard]] auto apply(const application_configuration& configuration,
        auto_configuration_context& context) const
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto contains(std::string_view name) const noexcept -> bool;
    [[nodiscard]] auto supports_multiple_event_loops(
        std::string_view name) const noexcept -> bool;

private:
    struct registration
    {
        service_auto_configurator configure;
        integration_loop_mode loop_mode;
    };
    std::map<std::string, registration, std::less<>> configurators_;
};

/**
 * @brief Registers only integrations compiled into the current cnetmod build.
 */
export void register_builtin_auto_configurations(
    auto_configuration_registry& registry);

} // namespace cnetmod::application
