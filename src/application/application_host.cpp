module cnetmod.application.host;

import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.recovery_policy;
import cnetmod.application.service_lifecycle;
import cnetmod.application.task_supervisor;
import cnetmod.core.crash_dump;
import cnetmod.core.log;
import cnetmod.core.net_init;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.observability.otlp;
import cnetmod.observability.http_server;
import cnetmod.protocol.http.middleware;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::application {

namespace {

    auto exporter_options(const observability_configuration& configuration)
        -> observability::otlp_http_options
    {
        auto options = configuration.otlp;
        options.export_traces = options.export_traces && configuration.tracing;
        options.export_metrics = options.export_metrics && configuration.metrics;
        options.export_logs = options.export_logs && configuration.logs;
        return options;
    }

} // namespace

class application_host::implementation
{
public:
    implementation(application_configuration configuration,
        http::router routes, service_registry services,
        bool auto_configuration,
        std::optional<std::filesystem::path> configuration_file)
        : configuration(std::move(configuration)), network(), io(make_io_context()), telemetry(*io, exporter_options(this->configuration.observability)), business_server(*io), management_server(*io), services(std::move(services)), health(this->configuration.health), supervisor(*io), lifecycle(*io, telemetry, this->services, supervisor, health, this->configuration.lifecycle), business_routes(std::move(routes)), configuration_file(std::move(configuration_file))
    {
        telemetry.set_sampling_ratio(this->configuration.observability.sampling_ratio);
        if (auto_configuration)
        {
            register_builtin_auto_configurations(auto_configurations);
            auto_configuration_context context{*io, telemetry, supervisor,
                this->services, business_routes};
            preparation_error = auto_configurations.apply(this->configuration,
                context);
        }
        this->services.freeze();
        if (preparation_error)
        {
            for (const auto& service : this->services.managed_services())
            {
                auto added = health.add(service);
                if (!added)
                {
                    preparation_error = std::unexpected(added.error());
                    break;
                }
            }
        }
        supervisor.on_recovery_exhausted(
            [this](std::string_view task, std::error_code error)
            {
                request_stop();
                try
                {
                    logger::error("required task {} exhausted recovery: {}", task,
                        error.message());
                }
                catch (...)
                {
                    /**
                     * Reporting must not escape the recovery notification.
                     */
                }
            });
        finish_task = finish();
        root_task = supervise();
    }

    void install_application_middleware()
    {
        if (configuration.http.recover_exceptions)
            business_server.use(recover());
        business_server.use(shutdown.track_middleware());
        if (configuration.http.request_ids)
            business_server.use(request_id());
        if (auto options = telemetry.server_tracing(); options.on_end)
            business_server.use(http::tracing::tracing_middleware(
                std::move(options)));
        if (auto middleware = observability::server_metrics(telemetry.measurements()))
            business_server.use(std::move(middleware));
        if (configuration.http.request_timeout)
            business_server.use(
                request_timeout(*configuration.http.request_timeout));
        if (configuration.http.access_logging)
            business_server.use(access_log({
                .lv = configuration.logging.level,
                .format = access_log_format::brief,
                .dump = access_log_dump::error_only,
                .redact_sensitive_headers = true,
            }));
    }

    void install_management_routes(http::router& routes)
    {
        routes.get(configuration.management.live_path,
            [this](http::request_context& request) -> task<void>
            {
                request.json(health.live() ? http::status::ok
                                           : http::status::service_unavailable,
                    health.json());
                co_return;
            });
        routes.get(configuration.management.ready_path,
            [this](http::request_context& request) -> task<void>
            {
                request.json(health.ready() ? http::status::ok
                                            : http::status::service_unavailable,
                    health.json(true));
                co_return;
            });
        routes.get(configuration.management.health_path,
            [this](http::request_context& request) -> task<void>
            {
                request.json(health.ready() ? http::status::ok
                                            : http::status::service_unavailable,
                    health.json());
                co_return;
            });
        if (configuration.observability.metrics &&
            !configuration.management.metrics_path.empty())
            routes.get(configuration.management.metrics_path,
                metrics::openmetrics_handler(telemetry.metrics()));
    }

    auto health_loop(cancel_token& token)
        -> task<std::expected<void, std::error_code>>
    {
        while (!token.is_cancelled())
        {
            const auto policy = health.policy();
            service_context context{*io, telemetry, supervisor, token,
                deadline::after(policy.timeout)};
            co_await health.refresh(context);
            for (const auto& snapshot : health.snapshots())
            {
                auto scheduled = lifecycle.reconcile_health(snapshot);
                if (!scheduled && scheduled.error() != std::make_error_code(std::errc::file_exists))
                {
                    try
                    {
                        logger::warn("failed to schedule recovery for {}: {}",
                            snapshot.key.canonical_name(),
                            scheduled.error().message());
                    }
                    catch (...)
                    {
                        /**
                         * Diagnostic failure must not stop subsequent health checks.
                         */
                    }
                }
            }
            telemetry.refresh_exporter_metrics();
            auto waited = co_await async_timer_wait(*io,
                policy.interval, token);
            if (!waited && !token.is_cancelled())
                co_return std::unexpected(waited.error());
        }
        co_return {};
    }

    /**
     * @brief Contains orchestration exceptions and enters owned cleanup once.
     */
    auto supervise() -> task<void>
    {
        try
        {
            co_await post_awaitable{*io};
            co_await run_lifecycle();
            co_return;
        }
        catch (const std::system_error& error)
        {
            if (!run_error)
                run_error = error.code();
        }
        catch (const std::bad_alloc&)
        {
            if (!run_error)
                run_error = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (...)
        {
            if (!run_error)
                run_error = std::make_error_code(std::errc::io_error);
        }
        shutdown_deadline = shutdown_deadline.constrain(
            deadline::after(configuration.lifecycle.total_stop_timeout));
        state.store(application_state::stopping, std::memory_order_release);
        health.mark_stopping();
        business_server.stop();
        management_server.stop();
        // Exceptional cleanup cannot wait for another normal drain phase.
        shutdown.cancel_requests();
        business_server.abort_connections();
        management_server.abort_connections();
        try
        {
            if (finish_task.handle() && !finish_task.handle().done())
            {
                co_await std::move(finish_task);
                co_return;
            }
        }
        catch (...)
        {
        }
        supervisor.request_stop();
        telemetry.abort();
        business_server.abort_connections();
        management_server.abort_connections();
        complete();
    }

    /**
     * @brief Registers an accept loop whose stop is issued on the host I/O thread.
     */
    auto supervise_listener(std::string name, http::server& listener)
        -> std::expected<void, std::error_code>
    {
        return supervisor.supervise(std::move(name), [this, &listener](cancel_token& token) -> task<std::expected<void, std::error_code>>
            {
                if (token.is_cancelled())
                    co_return {};
                co_await listener.run();
                if (!token.is_cancelled() && state.load(std::memory_order_acquire) == application_state::running)
                    co_return std::unexpected(std::make_error_code(std::errc::io_error));
                co_return {};
            },
            {.budget = std::chrono::milliseconds::zero()}, true);
    }

    auto run_lifecycle() -> task<void>
    {
        health.mark_starting();
        auto started = co_await lifecycle.start(configuration.lifecycle.total_stop_timeout / 5);
        if (!started)
        {
            run_error = started.error();
            shutdown_deadline = lifecycle.rollback_deadline().constrain(
                deadline::after(configuration.lifecycle.total_stop_timeout));
            co_await std::move(finish_task);
            co_return;
        }

        auto business_listening = business_server.listen(
            configuration.http.address, configuration.http.port);
        if (!business_listening)
        {
            run_error = business_listening.error();
            shutdown_deadline = deadline::after(configuration.lifecycle.total_stop_timeout);
            (void)co_await lifecycle.stop(shutdown_cleanup_deadline());
            co_await std::move(finish_task);
            co_return;
        }

        install_application_middleware();
        if (configuration.management.enabled &&
            configuration.management.same_port)
            install_management_routes(business_routes);
        business_server.set_max_connections(
            configuration.http.max_connections);
        business_server.set_router(std::move(business_routes));

        if (configuration.management.enabled &&
            !configuration.management.same_port)
        {
            http::router routes;
            install_management_routes(routes);
            management_server.set_router(std::move(routes));
            auto listening = management_server.listen(
                configuration.management.address,
                configuration.management.port);
            if (!listening)
            {
                run_error = listening.error();
                shutdown_deadline = deadline::after(configuration.lifecycle.total_stop_timeout);
                business_server.stop();
                (void)co_await lifecycle.stop(shutdown_cleanup_deadline());
                co_await std::move(finish_task);
                co_return;
            }
            const auto registered = supervise_listener("application-management-http", management_server);
            if (!registered)
                throw std::system_error(registered.error());
        }

        if (configuration.install_signal_handlers)
            shutdown.install();
        state.store(application_state::running, std::memory_order_release);
        health.mark_running();
        const auto registered = supervise_listener("application-business-http", business_server);
        if (!registered)
            throw std::system_error(registered.error());
        auto monitored = supervisor.supervise("application-health", [this](cancel_token& token)
            {
                return health_loop(token);
            },
            {}, false);
        if (!monitored)
            throw std::system_error(monitored.error());
        try
        {
            logger::info("{} listening on {}:{}", configuration.name,
                configuration.http.address, configuration.http.port);
        }
        catch (...)
        {
            /**
             * A ready application must not roll back because logging failed.
             */
        }

        auto sleeper = [this](auto duration)
        {
            return async_sleep(*io, duration);
        };
        co_await shutdown.wait_for_signal(sleeper);
        const auto stop_deadline = deadline::after(configuration.lifecycle.total_stop_timeout);
        shutdown_deadline = stop_deadline;
        const auto cleanup_deadline = shutdown_cleanup_deadline();
        state.store(application_state::stopping, std::memory_order_release);
        health.mark_stopping();
        business_server.stop();
        management_server.stop();
        const auto drained = co_await shutdown.drain(sleeper,
            std::min(configuration.lifecycle.http_drain_timeout,
                std::chrono::duration_cast<std::chrono::milliseconds>(cleanup_deadline.remaining())));
        if (!drained)
        {
            if (!run_error)
                run_error = std::make_error_code(std::errc::timed_out);
            shutdown.cancel_requests();
            business_server.abort_connections();
            management_server.abort_connections();
        }
        if (shutdown.in_flight() != 0)
            co_await settle_requests();
        supervisor.request_stop();
        auto joined = co_await supervisor.join();
        if (!joined && !run_error)
            run_error = joined.error();
        if (shutdown.in_flight() == 0)
        {
            auto stopped = co_await lifecycle.stop(cleanup_deadline);
            if (!stopped && !run_error)
                run_error = stopped.error();
        }
        co_await std::move(finish_task);
    }

    /**
     * @brief Reserves global shutdown budget for final connection cancellation.
     * @details The reserve is fixed relative to the configured total budget, so
     * successive cleanup phases cannot consume it by renewing their allowance.
     */
    auto shutdown_cleanup_deadline() const noexcept -> deadline
    {
        const auto reserve = cleanup_reserve.value_or(
            std::chrono::duration_cast<deadline::duration>(configuration.lifecycle.total_stop_timeout) / 5);
        return shutdown_deadline.constrain(deadline::after(
            std::max(deadline::duration::zero(), shutdown_deadline.remaining() - reserve)));
    }

    /**
     * @brief Keeps dependencies alive while handlers unwind within cleanup budget.
     */
    auto settle_requests() -> task<void>
    {
        const auto cleanup_deadline = shutdown_cleanup_deadline();
        while (shutdown.in_flight() != 0)
        {
            if (cleanup_deadline.expired())
            {
                if (!run_error)
                    run_error = std::make_error_code(std::errc::timed_out);
                co_return;
            }
            const auto waited = co_await async_timer_wait(*io,
                std::min(cleanup_deadline.remaining(),
                    std::chrono::duration_cast<deadline::duration>(std::chrono::milliseconds{1})));
            if (!waited)
            {
                if (!run_error)
                    run_error = waited.error();
                co_return;
            }
        }
    }

    /**
     * @brief Settles telemetry for both startup rollback and normal shutdown.
     */
    auto finish() -> task<void>
    {
        if (shutdown.in_flight() != 0)
            co_await settle_requests();
        supervisor.request_stop();
        const auto joined = co_await supervisor.join();
        if (!joined && !run_error)
            run_error = joined.error();
        auto retry_delay = std::chrono::milliseconds{1};
        const auto cleanup_deadline = shutdown_cleanup_deadline();
        while (shutdown.in_flight() == 0 && lifecycle.active_service_count() != 0 && !cleanup_deadline.expired())
        {
            try
            {
                const auto stopped = co_await lifecycle.stop(cleanup_deadline);
                if (!stopped && !run_error)
                    run_error = stopped.error();
            }
            catch (const std::system_error& error)
            {
                if (!run_error)
                    run_error = error.code();
            }
            catch (const std::bad_alloc&)
            {
                if (!run_error)
                    run_error = std::make_error_code(std::errc::not_enough_memory);
            }
            catch (...)
            {
                if (!run_error)
                    run_error = std::make_error_code(std::errc::io_error);
            }
            if (lifecycle.active_service_count() == 0)
                break;
            try
            {
                const auto waited = co_await async_timer_wait(*io,
                    std::min(cleanup_deadline.remaining(),
                        std::chrono::duration_cast<deadline::duration>(retry_delay)));
                if (!waited)
                    break;
            }
            catch (...)
            {
                break;
            }
            retry_delay = std::min(retry_delay * 2, std::chrono::milliseconds{20});
        }
        const auto telemetry_budget = std::min(configuration.lifecycle.telemetry_flush_timeout,
            std::chrono::duration_cast<std::chrono::milliseconds>(cleanup_deadline.remaining()));
        const auto cancellation_budget = std::max(std::chrono::milliseconds{1}, telemetry_budget / 5);
        std::expected<void, std::error_code> flushed;
        try
        {
            flushed = co_await telemetry.shutdown(
                std::max(std::chrono::milliseconds::zero(), telemetry_budget - cancellation_budget), cancellation_budget);
        }
        catch (const std::bad_alloc&)
        {
            telemetry.abort();
            flushed = std::unexpected(std::make_error_code(std::errc::not_enough_memory));
        }
        catch (...)
        {
            telemetry.abort();
            flushed = std::unexpected(std::make_error_code(std::errc::io_error));
        }
        if (!flushed)
        {
            try
            {
                logger::warn("{} telemetry shutdown failed: {}", configuration.name,
                    flushed.error().message());
            }
            catch (...)
            {
                // Failure reporting must not prevent shutdown under memory pressure.
            }
        }
        // Telemetry may have used this application's HTTP endpoint as its
        // collector. Its client closes during shutdown, so the corresponding
        // server EOF completion must run before the event loop is stopped.
        /**
         * Reserve existing budget for socket cancellation completions. Aborting
         * only at the global deadline would stop the event loop before pending
         * connection frames have a chance to release their resources.
         */
        const auto connection_budget = shutdown_deadline.remaining();
        const auto connection_cancellation_budget = connection_budget / 5;
        auto connection_deadline = shutdown_deadline.constrain(
            deadline::after(std::min(
                std::chrono::duration_cast<deadline::duration>(configuration.lifecycle.http_drain_timeout),
                connection_budget - connection_cancellation_budget)));
        bool connections_aborted = false;
        try
        {
            while (!telemetry.try_settle_shutdown() ||
                business_server.active_connections() != 0 ||
                management_server.active_connections() != 0)
            {
                const auto remaining = connection_deadline.remaining();
                if (connection_deadline.expired())
                {
                    if (!run_error)
                        run_error = std::make_error_code(std::errc::timed_out);
                    if (!connections_aborted)
                    {
                        business_server.abort_connections();
                        management_server.abort_connections();
                        connections_aborted = true;
                        connection_deadline = shutdown_deadline.constrain(
                            deadline::after(configuration.lifecycle.service_stop_timeout));
                        continue;
                    }
                    break;
                }
                const auto waited = co_await async_timer_wait(*io,
                    std::min(remaining, std::chrono::duration_cast<deadline::duration>(std::chrono::milliseconds{1})));
                if (!waited)
                {
                    if (!run_error)
                        run_error = waited.error();
                    break;
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            if (!run_error)
                run_error = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (...)
        {
            if (!run_error)
                run_error = std::make_error_code(std::errc::io_error);
        }
        complete();
    }

    void request_stop() noexcept
    {
        const auto current = state.load(std::memory_order_acquire);
        if (current == application_state::starting ||
            current == application_state::running)
            shutdown.request_stop();
    }

    /**
     * @brief Contains retry failures while retaining the original run diagnostic.
     */
    auto retry_finish() -> task<void>
    {
        try
        {
            co_await finish();
            co_return;
        }
        catch (const std::system_error& error)
        {
            cleanup_error = error.code();
        }
        catch (const std::bad_alloc&)
        {
            cleanup_error = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (...)
        {
            cleanup_error = std::make_error_code(std::errc::io_error);
        }
        complete();
    }

    void complete() noexcept
    {
        telemetry.close();
        shutdown.uninstall();
        const bool retained = !telemetry.try_settle_shutdown() || lifecycle.active_service_count() != 0 ||
            business_server.active_connections() != 0 || management_server.active_connections() != 0;
        if (retained && !run_error)
            run_error = std::make_error_code(std::errc::timed_out);
        state.store(retained ? application_state::cleanup_failed : application_state::stopped,
            std::memory_order_release);
        io->stop();
    }

    application_configuration configuration;
    net_init network;
    std::unique_ptr<io_context> io;
    observability::telemetry_hub telemetry;
    http::server business_server;
    http::server management_server;
    service_registry services;
    health_registry health;
    task_supervisor supervisor;
    service_lifecycle lifecycle;
    http::router business_routes;
    shutdown_handler shutdown;
    auto_configuration_registry auto_configurations;
    std::optional<std::filesystem::path> configuration_file;
    std::expected<void, std::error_code> preparation_error{};
    std::atomic<application_state> state{application_state::built};
    std::optional<std::error_code> run_error;
    std::error_code cleanup_error;
    deadline shutdown_deadline;
    std::optional<deadline::duration> cleanup_reserve;
    bool logging_owned = false;
    mutable concurrent_containers::atomic_rw_latch configuration_latch;
    task<void> finish_task;
    task<void> root_task;
};

application_host::application_host(
    std::unique_ptr<implementation> implementation)
    : implementation_(std::move(implementation))
{
    // Install before logging, listeners, or background tasks.  This is a
    // process-level safety net and therefore intentionally not coupled to
    // the optional observability pipeline.
    crash_dump::set_app_name(implementation_->configuration.name);
    crash_dump::install(implementation_->configuration.crash_dump.directory.string());
    if (implementation_->configuration.logging.manage_lifecycle)
    {
        logger::init(implementation_->configuration.name,
            implementation_->configuration.logging.level,
            implementation_->configuration.logging.format);
        implementation_->logging_owned = true;
    }
}

application_host::application_host(application_host&&) noexcept = default;
auto application_host::operator=(application_host&&) noexcept
    -> application_host& = default;

application_host::~application_host()
{
    if (!implementation_)
        return;
    request_stop();
    implementation_->telemetry.close();
    if (implementation_->logging_owned)
        logger::shutdown();
}

auto application_host::run() -> std::expected<void, std::error_code>
{
    if (!implementation_->preparation_error)
        return std::unexpected(implementation_->preparation_error.error());
    auto expected = application_state::built;
    if (!implementation_->state.compare_exchange_strong(expected,
            application_state::starting, std::memory_order_acq_rel))
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));
    implementation_->root_task.handle().resume();
    implementation_->io->run();
    if (!implementation_->root_task.handle().done())
    {
        implementation_->state.store(application_state::cleanup_failed, std::memory_order_release);
        if (!implementation_->run_error)
            implementation_->run_error = std::make_error_code(std::errc::operation_canceled);
    }
    if (implementation_->run_error)
        return std::unexpected(*implementation_->run_error);
    return {};
}

void application_host::request_stop() noexcept
{
    if (implementation_)
        implementation_->request_stop();
}

auto application_host::retry_cleanup(std::chrono::milliseconds timeout)
    -> std::expected<void, std::error_code>
{
    if (!implementation_ || timeout <= std::chrono::milliseconds::zero() ||
        timeout > std::chrono::duration_cast<std::chrono::milliseconds>(deadline::duration::max() / 2))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto& host = *implementation_;
    const auto current = host.state.load(std::memory_order_acquire);
    if (current == application_state::stopped)
        return {};
    if (current != application_state::cleanup_failed)
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    if (!host.root_task.handle() || !host.root_task.handle().done())
        return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
    try
    {
        auto retry = host.retry_finish();
        auto expected = application_state::cleanup_failed;
        if (!host.state.compare_exchange_strong(expected, application_state::stopping,
                std::memory_order_acq_rel))
            return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
        host.cleanup_error.clear();
        host.shutdown_deadline = deadline::after(timeout);
        host.cleanup_reserve = std::chrono::duration_cast<deadline::duration>(timeout) / 5;
        host.root_task = std::move(retry);
        host.io->restart();
        host.root_task.handle().resume();
        host.io->run();
        if (!host.root_task.handle().done())
        {
            host.state.store(application_state::cleanup_failed, std::memory_order_release);
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        }
        if (host.cleanup_error)
            return std::unexpected(host.cleanup_error);
        if (host.state.load(std::memory_order_acquire) != application_state::stopped)
            return std::unexpected(std::make_error_code(std::errc::timed_out));
        return {};
    }
    catch (const std::bad_alloc&)
    {
        if (host.state.load(std::memory_order_acquire) == application_state::stopping)
            host.state.store(application_state::cleanup_failed, std::memory_order_release);
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (const std::system_error& error)
    {
        host.state.store(application_state::cleanup_failed, std::memory_order_release);
        return std::unexpected(error.code());
    }
    catch (...)
    {
        host.state.store(application_state::cleanup_failed, std::memory_order_release);
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
}

auto application_host::reload_configuration()
    -> std::expected<configuration_reload_result, std::error_code>
{
    if (!implementation_->configuration_file)
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_supported));
    try
    {
        auto candidate = load_configuration(implementation_->configuration_file);
        if (!candidate)
            return std::unexpected(candidate.error());
        std::vector<std::pair<service_key, recovery_policy>> recovery_updates;
        concurrent_containers::exclusive_latch_guard lock{
            implementation_->configuration_latch};
        recovery_updates.reserve(implementation_->configuration.services.size());
        for (const auto& [binding, service] : implementation_->configuration.services)
        {
            const auto replacement = candidate->services.find(binding);
            if (replacement != candidate->services.end() &&
                replacement->second.name == service.name &&
                replacement->second.instance == service.instance)
                recovery_updates.emplace_back(service_key{service.name, service.instance},
                    replacement->second.recovery);
        }
        auto staged = implementation_->configuration;
        auto reloaded = reload_safe_configuration(staged, *candidate);
        if (!reloaded)
            return std::unexpected(reloaded.error());
        const bool logging_changed = staged.logging.level != implementation_->configuration.logging.level;
        if (logging_changed)
            (void)logger::dropped_messages();
        auto policies_updated = implementation_->lifecycle.update_recovery_policies(recovery_updates);
        if (!policies_updated)
            return std::unexpected(policies_updated.error());
        implementation_->health.update_policy(staged.health);
        implementation_->telemetry.set_sampling_ratio(
            staged.observability.sampling_ratio);
        if (logging_changed)
            logger::set_level(staged.logging.level);
        static_assert(std::is_nothrow_move_assignable_v<application_configuration>);
        implementation_->configuration = std::move(staged);
        return std::move(*reloaded);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
}

auto application_host::state() const noexcept -> application_state
{
    return implementation_->state.load(std::memory_order_acquire);
}

auto application_host::last_failure() const
    -> std::optional<lifecycle_failure>
{
    return implementation_->lifecycle.last_failure();
}

auto application_host::configuration() const
    -> application_configuration
{
    concurrent_containers::shared_latch_guard lock{
        implementation_->configuration_latch};
    return implementation_->configuration;
}

auto application_host::services() noexcept -> service_registry&
{
    return implementation_->services;
}

auto application_host::health() noexcept -> health_registry&
{
    return implementation_->health;
}

auto application_host::telemetry() noexcept
    -> observability::telemetry_hub&
{
    return implementation_->telemetry;
}

application_builder::application_builder(std::string name)
    : name_(std::move(name))
{
    if (name_.empty())
        throw std::invalid_argument("application name cannot be empty");
}

auto application_builder::configuration_file(std::filesystem::path path)
    -> application_builder&
{
    configuration_file_ = std::move(path);
    return *this;
}

auto application_builder::configure(configuration_customizer customizer)
    -> application_builder&
{
    if (!customizer)
        throw std::invalid_argument("configuration customizer cannot be empty");
    customizers_.push_back(std::move(customizer));
    return *this;
}

auto application_builder::routes(route_configurer configurer)
    -> application_builder&
{
    if (!configurer)
        throw std::invalid_argument("route configurer cannot be empty");
    route_configurers_.push_back(std::move(configurer));
    return *this;
}

auto application_builder::service(std::shared_ptr<managed_service> service)
    -> application_builder&
{
    if (!service)
        throw std::invalid_argument("managed service cannot be empty");
    services_.push_back(std::move(service));
    return *this;
}

auto application_builder::enable_auto_configuration() noexcept
    -> application_builder&
{
    auto_configuration_ = true;
    return *this;
}

auto application_builder::build()
    -> std::expected<application_host, std::error_code>
{
    auto loaded = load_configuration(configuration_file_);
    if (!loaded)
        return std::unexpected(loaded.error());
    loaded->name = name_;
    try
    {
        for (const auto& customizer : customizers_)
            customizer(*loaded);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    auto valid = validate_configuration(*loaded);
    if (!valid)
        return std::unexpected(valid.error());

    service_registry registry;
    for (auto& service : services_)
    {
        auto added = registry.manage(service);
        if (!added)
            return std::unexpected(added.error());
    }
    http::router routes;
    try
    {
        for (const auto& configure_routes : route_configurers_)
            configure_routes(routes);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    try
    {
        application_host host{std::make_unique<application_host::implementation>(
            std::move(*loaded), std::move(routes), std::move(registry),
            auto_configuration_, configuration_file_)};
        if (!host.implementation_->preparation_error)
            return std::unexpected(
                host.implementation_->preparation_error.error());
        const auto graph = host.implementation_->services.validate_dependencies();
        if (!graph)
            return std::unexpected(graph.error());
        return host;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (const std::system_error& error)
    {
        return std::unexpected(error.code());
    }
}

} // namespace cnetmod::application
