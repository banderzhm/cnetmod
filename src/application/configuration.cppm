/**
 * @brief Strongly validated application configuration and reload classification.
 */
export module cnetmod.application.configuration;

import std;
import cnetmod.application.recovery_policy;
import cnetmod.json;
import cnetmod.application.health_registry;
import cnetmod.application.managed_service;
import cnetmod.application.service_lifecycle;
import cnetmod.core.log;
import cnetmod.observability.otlp;

namespace cnetmod::application {

/**
 * @brief Logging lifecycle, severity, and encoding settings.
 */
export struct logging_configuration
{
    bool manage_lifecycle = true;
    logger::level level = logger::level::info;
    logger::output_format format = logger::output_format::text;
};

/**
 * @brief Business HTTP server settings.
 */
export struct http_configuration
{
    std::string address{"0.0.0.0"};
    std::uint16_t port = 8080;
    std::size_t max_connections = 0;
    std::optional<std::chrono::milliseconds> request_timeout;
    std::chrono::milliseconds sse_max_duration{120000};
    std::chrono::milliseconds sse_write_timeout{5000};
    bool request_ids = true;
    bool access_logging = true;
    bool recover_exceptions = true;
};

/**
 * @brief Isolated management endpoint settings.
 */
export struct management_configuration
{
    bool enabled = true;
    std::string address{"127.0.0.1"};
    std::uint16_t port = 8081;
    bool same_port = false;
    std::string live_path{"/actuator/live"};
    std::string ready_path{"/actuator/ready"};
    std::string health_path{"/actuator/health"};
    std::string metrics_path{"/actuator/prometheus"};
};

/**
 * @brief Traces, metrics, logs, sampling, and OTLP export settings.
 */
export struct observability_configuration
{
    bool tracing = true;
    bool metrics = true;
    bool logs = true;
    double sampling_ratio = 1.0;
    observability::otlp_http_options otlp;
};

/**
 * @brief Process-crash artifact settings installed by every application host.
 *
 * Crash artifact capture is deliberately independent from logging and OTLP:
 * a fatal process failure must still leave an analyzable artifact when those
 * subsystems are disabled or already unhealthy.
 */
export struct crash_dump_configuration
{
    std::filesystem::path directory{"crash"};
};

/**
 * @brief Host-owned execution resources used by application workloads.
 */
export struct execution_configuration
{
    /// Business HTTP event loops. Values above one explicitly enable
    /// connection-affine multi-loop execution.
    unsigned io_threads = 1;
    unsigned cpu_threads = std::max(1U, std::thread::hardware_concurrency());

    auto operator==(const execution_configuration&) const -> bool = default;
};

/**
 * @brief One named, explicitly enabled external service definition.
 */
export struct configured_service
{
    std::string name;
    std::string instance{"default"};
    bool enabled = false;
    service_requirement requirement = service_requirement::required;
    recovery_policy recovery;
    cnetmod::json::document properties = cnetmod::json::object();

    /**
     * Reads a nested string property addressed by a dot-separated path.
     * Missing properties return an empty optional; type mismatches return an
     * invalid-argument error.
     */
    [[nodiscard]] auto string_property(std::string_view path) const
        -> std::expected<std::optional<std::string>, std::error_code>;

    /**
     * Reads a nested integer property addressed by a dot-separated path.
     */
    [[nodiscard]] auto integer_property(std::string_view path) const
        -> std::expected<std::optional<std::int64_t>, std::error_code>;

    /**
     * Reads a nested array containing only strings.
     */
    [[nodiscard]] auto string_array_property(std::string_view path) const
        -> std::expected<std::optional<std::vector<std::string>>,
            std::error_code>;

    /**
     * Sets one top-level string property without exposing the JSON backend.
     */
    void set_property(std::string name, std::string value);

    /**
     * Sets one top-level integer property without exposing the JSON backend.
     */
    void set_property(std::string name, std::int64_t value);

    /**
     * Sets one top-level Boolean property without exposing the JSON backend.
     */
    void set_property(std::string name, bool value);
};

/**
 * @brief One logical table's database and physical-table shard topology.
 */
export struct orm_shard_topology_configuration
{
    std::string logical_table;
    std::size_t table_count = 1;
    std::vector<std::string> databases;
    bool scatter_gather = true;
    bool distributed_transactions = true;

    auto operator==(const orm_shard_topology_configuration&) const -> bool = default;
};

/**
 * @brief Opt-in ORM shard auto-configuration.
 */
export struct orm_sharding_configuration
{
    bool enabled = false;
    std::map<std::string, orm_shard_topology_configuration, std::less<>> topologies;

    auto operator==(const orm_sharding_configuration&) const -> bool = default;
};

/**
 * @brief ORM runtime configuration.
 */
export struct orm_configuration
{
    orm_sharding_configuration sharding;
    /// Strict SaaS mode: repositories of tenant models reject operations that
    /// have no request tenant scope. Frozen at build time.
    bool tenant_scope_required = false;

    auto operator==(const orm_configuration&) const -> bool = default;
};

/**
 * @brief Validated JWT settings owned by the application configuration.
 *
 * Secrets are resolved by the central configuration pipeline and are never
 * read directly by middleware or business code.
 */
export struct jwt_configuration
{
    bool enabled = false;
    std::string issuer;
    std::string secret;
    std::int64_t expires_in_seconds = 604800;
    std::int64_t session_idle_seconds = 900;

    auto operator==(const jwt_configuration&) const -> bool = default;
};

/**
 * @brief Security settings shared by application infrastructure.
 */
export struct security_configuration
{
    jwt_configuration jwt;

    auto operator==(const security_configuration&) const -> bool = default;
};

/**
 * @brief Complete immutable-at-runtime application configuration model.
 */
export struct application_configuration
{
    std::string name{"cnetmod-application"};
    logging_configuration logging;
    http_configuration http;
    management_configuration management;
    observability_configuration observability;
    crash_dump_configuration crash_dump;
    execution_configuration execution;
    lifecycle_policy lifecycle;
    health_policy health;
    orm_configuration orm;
    security_configuration security;
    bool install_signal_handlers = true;
    std::map<std::string, configured_service, std::less<>> services;
    /// Application-defined top-level sections, captured verbatim after
    /// environment expansion. Every section must be claimed by a registered
    /// options section, otherwise the application build fails.
    std::map<std::string, cnetmod::json::document, std::less<>> sections;
};

/**
 * @brief Structured configuration failure with the offending document path.
 */
export struct configuration_error
{
    std::error_code code = std::make_error_code(std::errc::invalid_argument);
    /// Dotted document path, for example "http.sse.max_duration_ms", or
    /// "env:NAME" for environment overlays. Empty for whole-file failures.
    std::string path;
    std::string message;

    /**
     * @brief Returns "path: message" suitable for logs and terminal output.
     */
    [[nodiscard]] auto describe() const -> std::string;
};

/**
 * @brief Classification of a runtime configuration reload.
 */
export struct configuration_reload_result
{
    bool applied = false;
    bool restart_required = false;
    std::vector<std::string> changed;
    /// Application sections whose content differs from the active snapshot.
    /// The options registry classifies and publishes them.
    std::vector<std::string> changed_sections;
};

/**
 * @brief Loads defaults, YAML/JSON, and environment configuration layers.
 * @param file Optional YAML (.yaml/.yml) or JSON configuration path.
 * @return Validated configuration or a parsing, validation, or resource error.
 * Propagating allocation failures are translated to not_enough_memory.
 */
export [[nodiscard]] auto load_configuration(
    const std::optional<std::filesystem::path>& file)
    -> std::expected<application_configuration, configuration_error>;

/**
 * @brief Validates all cross-field and value constraints before startup.
 */
export [[nodiscard]] auto validate_configuration(
    const application_configuration& configuration)
    -> std::expected<void, configuration_error>;

/**
 * @brief Applies hot-reloadable fields and classifies restart-only changes.
 * Validates and prepares a private snapshot before committing configuration.
 * Preparation failure leaves the active configuration unchanged.
 * This function does not change the process logger or runtime services.
 */
export [[nodiscard]] auto reload_safe_configuration(
    application_configuration& active,
    const application_configuration& candidate)
    -> std::expected<configuration_reload_result, configuration_error>;

/**
 * @brief Expands ${NAME} and ${NAME:-default} references in one string.
 *
 * "$${" produces a literal "${". A missing variable without a default is an
 * error naming the variable; an empty value counts as missing.
 */
export [[nodiscard]] auto expand_environment_references(std::string_view source)
    -> std::expected<std::string, configuration_error>;

/**
 * @brief Returns a recursively redacted copy suitable for diagnostics.
 */
export [[nodiscard]] auto redact_configuration(
    const cnetmod::json::document& value) -> cnetmod::json::document;

} // namespace cnetmod::application
