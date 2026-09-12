/// Batteries-included HTTP application composition root.
export module cnetmod.application.http;

import std;
import cnetmod.application.configuration;
import cnetmod.application.lifecycle;
import cnetmod.application.service_registry;
import cnetmod.core.net_init;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware;

namespace cnetmod::application {

export enum class application_state
{
    configuring,
    starting,
    running,
    stopping,
    stopped
};

export class http_application
{
public:
    explicit http_application(application_options options = {});
    ~http_application();

    http_application(const http_application&) = delete;
    auto operator=(const http_application&) -> http_application& = delete;
    http_application(http_application&&) = delete;
    auto operator=(http_application&&) -> http_application& = delete;

    [[nodiscard]] auto options() const noexcept -> const application_options&;
    [[nodiscard]] auto context() noexcept -> io_context&;
    [[nodiscard]] auto routes() -> http::router&;
    [[nodiscard]] auto services() noexcept -> service_registry&;
    [[nodiscard]] auto lifecycle() -> application_lifecycle&;
    [[nodiscard]] auto telemetry() noexcept -> observability::telemetry_hub&;
    [[nodiscard]] auto state() const noexcept -> application_state;

    /// Register application middleware after the built-in safety middleware.
    /// Configuration is closed as soon as run() begins.
    void use(http::middleware_fn middleware);

    /// Bind, start lifecycle hooks, run the event loop, then drain resources.
    [[nodiscard]] auto run() -> std::expected<void, std::error_code>;

    /// Thread-safe request for graceful shutdown. The event loop remains alive
    /// until in-flight requests and lifecycle hooks have been drained.
    void stop() noexcept;

private:
    [[nodiscard]] static auto normalize(application_options options)
        -> application_options;
    void ensure_configuring() const;
    void install_management_routes();
    void install_middleware();
    auto supervise() -> task<void>;

    application_options options_;
    net_init network_;
    std::unique_ptr<io_context> context_;
    observability::telemetry_hub telemetry_;
    http::server server_;
    http::router router_;
    std::vector<http::middleware_fn> middleware_;
    service_registry services_;
    application_lifecycle lifecycle_;
    shutdown_handler shutdown_;
    std::atomic<application_state> state_{application_state::configuring};
    std::optional<std::error_code> run_error_;
    bool logging_owned_ = false;
};

export using application_configurer = std::function<void(http_application&)>;

/// Compact main() entry point for applications that only need configuration
/// plus route/service registration.
export [[nodiscard]] auto run_application(application_options options = {},
    application_configurer configure = {})
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
