/// Externalized configuration for the batteries-included HTTP application.
export module cnetmod.application.configuration;

import std;
import cnetmod.core.log;
import cnetmod.observability.otlp;

namespace cnetmod::application {

export struct logging_options
{
    bool manage_lifecycle = true;
    logger::level level = logger::level::info;
    logger::output_format format = logger::output_format::text;
};

export struct http_server_options
{
    std::string address{"0.0.0.0"};
    std::uint16_t port = 8080;
    std::size_t max_connections = 0;
    std::optional<std::chrono::milliseconds> request_timeout;
    bool request_ids = true;
    bool access_logging = true;
    bool recover_exceptions = true;
};

export struct management_options
{
    bool enabled = true;
    std::string health_path{"/actuator/health"};
    std::string metrics_path{"/actuator/prometheus"};
};

export struct observability_options
{
    bool tracing = true;
    observability::otlp_http_options otlp;
};

export struct application_options
{
    std::string name{"cnetmod-application"};
    logging_options logging;
    http_server_options http;
    management_options management;
    observability_options observability;
    bool install_signal_handlers = true;
    std::chrono::milliseconds shutdown_timeout = std::chrono::seconds{10};
};

/// Overlay conventional process environment variables on caller defaults.
/// Supported variables: CNETMOD_APPLICATION_NAME, CNETMOD_HTTP_ADDRESS,
/// CNETMOD_HTTP_PORT, standard OTEL endpoint/service/resource/header variables.
export [[nodiscard]] auto load_application_options(
    application_options defaults = {}) -> application_options;

} // namespace cnetmod::application
