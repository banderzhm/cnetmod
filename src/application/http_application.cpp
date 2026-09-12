module cnetmod.application.http;

import std;
import cnetmod.core.log;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;

namespace cnetmod::application {

auto http_application::normalize(application_options options)
    -> application_options
{
    options = load_application_options(std::move(options));
    if (options.observability.otlp.service_name.empty() ||
        options.observability.otlp.service_name == "cnetmod")
        options.observability.otlp.service_name = options.name;
    return options;
}

http_application::http_application(application_options options)
    : options_(normalize(std::move(options))), network_(), context_(make_io_context()), telemetry_(*context_, options_.observability.otlp), server_(*context_)
{
    if (options_.logging.manage_lifecycle)
    {
        logger::init(options_.name, options_.logging.level,
            options_.logging.format);
        logging_owned_ = true;
    }
}

http_application::~http_application()
{
    stop();
    telemetry_.close();
    if (logging_owned_)
        logger::shutdown();
}

auto http_application::options() const noexcept -> const application_options&
{
    return options_;
}

auto http_application::context() noexcept -> io_context&
{
    return *context_;
}

auto http_application::routes() -> http::router&
{
    ensure_configuring();
    return router_;
}

auto http_application::services() noexcept -> service_registry&
{
    return services_;
}

auto http_application::lifecycle() -> application_lifecycle&
{
    ensure_configuring();
    return lifecycle_;
}

auto http_application::telemetry() noexcept
    -> observability::telemetry_hub&
{
    return telemetry_;
}

auto http_application::state() const noexcept -> application_state
{
    return state_.load(std::memory_order_acquire);
}

void http_application::use(http::middleware_fn middleware)
{
    ensure_configuring();
    if (!middleware)
        throw std::invalid_argument("application middleware cannot be empty");
    middleware_.push_back(std::move(middleware));
}

void http_application::ensure_configuring() const
{
    if (state() != application_state::configuring)
        throw std::logic_error("application configuration is already closed");
}

void http_application::install_management_routes()
{
    if (!options_.management.enabled)
        return;
    if (!options_.management.health_path.empty())
        router_.get(options_.management.health_path, health_check());
    if (!options_.management.metrics_path.empty())
        router_.get(options_.management.metrics_path,
            metrics::openmetrics_handler(telemetry_.metrics()));
}

void http_application::install_middleware()
{
    if (options_.http.recover_exceptions)
        server_.use(recover());
    server_.use(shutdown_.track_middleware());
    if (options_.http.request_ids)
        server_.use(request_id());
    if (options_.observability.tracing &&
        !options_.observability.otlp.endpoint.empty())
        server_.use(http::tracing::tracing_middleware(
            telemetry_.server_tracing()));
    server_.use(metrics::openmetrics_middleware(telemetry_.metrics()));
    if (options_.http.request_timeout)
        server_.use(request_timeout(*options_.http.request_timeout));
    if (options_.http.access_logging)
    {
        server_.use(access_log({
            .lv = options_.logging.level,
            .format = access_log_format::brief,
            .dump = access_log_dump::error_only,
            .log_request_headers = false,
            .log_request_body = false,
            .log_response_headers = false,
            .log_response_body = false,
            .redact_sensitive_headers = true,
        }));
    }
    for (auto& middleware : middleware_)
        server_.use(std::move(middleware));
}

auto http_application::run() -> std::expected<void, std::error_code>
{
    auto expected = application_state::configuring;
    if (!state_.compare_exchange_strong(expected, application_state::starting,
            std::memory_order_acq_rel))
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));

    install_management_routes();
    install_middleware();
    server_.set_max_connections(options_.http.max_connections);
    server_.set_router(std::move(router_));
    auto listening = server_.listen(options_.http.address, options_.http.port);
    if (!listening)
    {
        state_.store(application_state::stopped, std::memory_order_release);
        return std::unexpected(listening.error());
    }
    if (options_.install_signal_handlers)
        shutdown_.install();

    spawn(*context_, supervise());
    context_->run();
    state_.store(application_state::stopped, std::memory_order_release);
    if (run_error_)
        return std::unexpected(*run_error_);
    return {};
}

void http_application::stop() noexcept
{
    const auto current = state();
    if (current == application_state::starting ||
        current == application_state::running)
        shutdown_.request_stop();
}

auto http_application::supervise() -> task<void>
{
    try
    {
        auto started = co_await lifecycle_.start();
        if (!started)
        {
            run_error_ = started.error();
            state_.store(application_state::stopping,
                std::memory_order_release);
            server_.stop();
            (void)co_await lifecycle_.stop();
            telemetry_.close();
            shutdown_.uninstall();
            context_->stop();
            co_return;
        }

        services_.freeze();
        state_.store(application_state::running, std::memory_order_release);
        spawn(*context_, server_.run());
        logger::info("{} listening on {}:{}", options_.name,
            options_.http.address, options_.http.port);

        auto sleeper = [this](auto duration)
        {
            return async_sleep(*context_, duration);
        };
        co_await shutdown_.wait_for_signal(sleeper);
        state_.store(application_state::stopping, std::memory_order_release);
        server_.stop();
        (void)co_await shutdown_.drain(sleeper, options_.shutdown_timeout);

        auto stopped = co_await lifecycle_.stop();
        if (!stopped)
        {
            run_error_ = stopped.error();
            logger::error("{} shutdown hook failed: {}", options_.name,
                stopped.error().message());
        }
        auto flushed = co_await telemetry_.flush(options_.shutdown_timeout);
        if (!flushed)
            logger::warn("{} telemetry flush did not complete: {}",
                options_.name, flushed.error().message());
        telemetry_.close();
        shutdown_.uninstall();
        context_->stop();
    }
    catch (const std::exception& error)
    {
        run_error_ = std::make_error_code(std::errc::io_error);
        logger::error("{} application lifecycle failed: {}", options_.name,
            error.what());
        server_.stop();
        telemetry_.close();
        shutdown_.uninstall();
        context_->stop();
    }
    catch (...)
    {
        run_error_ = std::make_error_code(std::errc::io_error);
        logger::error("{} application lifecycle failed", options_.name);
        server_.stop();
        telemetry_.close();
        shutdown_.uninstall();
        context_->stop();
    }
}

auto run_application(application_options options,
    application_configurer configure)
    -> std::expected<void, std::error_code>
{
    http_application application{std::move(options)};
    if (configure)
        configure(application);
    return application.run();
}

} // namespace cnetmod::application
