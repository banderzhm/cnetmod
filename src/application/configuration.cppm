/**
 * @brief Strongly validated application configuration and reload classification.
 */
export module cnetmod.application.configuration;

import std;
import cnetmod.application.recovery_policy;
import nlohmann.json;
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
 * @brief One named, explicitly enabled external service definition.
 */
export struct configured_service
{
    std::string name;
    std::string instance{"default"};
    bool enabled = false;
    service_requirement requirement = service_requirement::required;
    recovery_policy recovery;
    nlohmann::json properties = nlohmann::json::object();
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
    lifecycle_policy lifecycle;
    health_policy health;
    bool install_signal_handlers = true;
    std::map<std::string, configured_service, std::less<>> services;
};

/**
 * @brief Classification of a runtime configuration reload.
 */
export struct configuration_reload_result
{
    bool applied = false;
    bool restart_required = false;
    std::vector<std::string> changed;
};

/**
 * @brief Loads defaults, JSON, and environment configuration layers.
 * @param file Optional JSON configuration path.
 * @return Validated configuration or a parsing, validation, or resource error.
 * Propagating allocation failures are translated to not_enough_memory.
 */
export [[nodiscard]] auto load_configuration(
    const std::optional<std::filesystem::path>& file)
    -> std::expected<application_configuration, std::error_code>;

/**
 * @brief Validates all cross-field and value constraints before startup.
 */
export [[nodiscard]] auto validate_configuration(
    const application_configuration& configuration)
    -> std::expected<void, std::error_code>;

/**
 * @brief Applies hot-reloadable fields and classifies restart-only changes.
 * Validates and prepares a private snapshot before committing configuration.
 * Preparation failure leaves the active configuration unchanged.
 * This function does not change the process logger or runtime services.
 */
export [[nodiscard]] auto reload_safe_configuration(
    application_configuration& active,
    const application_configuration& candidate)
    -> std::expected<configuration_reload_result, std::error_code>;

/**
 * @brief Returns a recursively redacted copy suitable for diagnostics.
 */
export [[nodiscard]] auto redact_configuration(
    const nlohmann::json& value) -> nlohmann::json;

} // namespace cnetmod::application
