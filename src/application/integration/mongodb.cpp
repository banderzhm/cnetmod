module cnetmod.application.mongodb;

#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.application.task_supervisor;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;

namespace cnetmod::application {

namespace {
    /**
     * Preserves MongoDB failure identity without publishing server diagnostics.
     * Values are offset by one because the protocol enum has no success entry.
     */
    class mongodb_category final : public std::error_category
    {
    public:
        auto name() const noexcept -> const char* override
        {
            return "cnetmod.mongodb";
        }

        auto message(int value) const -> std::string override
        {
            return std::format("MongoDB error {}", value);
        }

        auto default_error_condition(int value) const noexcept -> std::error_condition override
        {
            if (value <= 0 || value > static_cast<int>(mongodb::error_code::connection_closed) + 1)
                return {value, *this};
            switch (static_cast<mongodb::error_code>(value - 1))
            {
            case mongodb::error_code::invalid_bson:
            case mongodb::error_code::protocol_error:
                return std::make_error_condition(std::errc::protocol_error);
            case mongodb::error_code::message_too_large:
                return std::make_error_condition(std::errc::message_size);
            case mongodb::error_code::authentication_failed:
                return std::make_error_condition(std::errc::permission_denied);
            case mongodb::error_code::connection_failed:
            case mongodb::error_code::tls_failed:
                return std::make_error_condition(std::errc::io_error);
            case mongodb::error_code::connection_closed:
                return std::make_error_condition(std::errc::not_connected);
            case mongodb::error_code::pool_exhausted:
                return std::make_error_condition(std::errc::resource_unavailable_try_again);
            default:
                return {value, *this};
            }
        }
    };

    auto lifecycle_error(mongodb::error_code code) noexcept -> std::error_code
    {
        if (code == mongodb::error_code::operation_cancelled)
            return std::make_error_code(std::errc::operation_canceled);
        if (code == mongodb::error_code::operation_timed_out)
            return std::make_error_code(std::errc::timed_out);
        static const mongodb_category category;
        return {static_cast<int>(code) + 1, category};
    }

    auto warm_pool(mongodb::connection_pool& pool, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto result = co_await pool.warm_up(cancellation);
        if (!result)
            co_return std::unexpected(lifecycle_error(result.error().code));
        co_return {};
    }

    auto probe_pool(mongodb::connection_pool& pool, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto result = co_await pool.health_check(cancellation);
        if (!result)
            co_return std::unexpected(lifecycle_error(result.error().code));
        co_return {};
    }
} // namespace

mongodb_service::mongodb_service(io_context& io,
    mongodb::connection_pool_options options, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : options_(std::move(options)), pool_(std::make_shared<mongodb::connection_pool>(io, options_)), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery)
{
}

auto mongodb_service::pool() noexcept -> mongodb::connection_pool&
{
    return *pool_;
}

auto mongodb_service::key() const -> service_key
{
    return {"mongodb", instance_};
}

auto mongodb_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto mongodb_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto mongodb_service::cleanup_required() const noexcept -> bool
{
    return cleanup_pending_;
}

auto mongodb_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    if (cleanup_pending_)
        co_return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
    if (started_)
        co_return {};
    std::expected<void, std::error_code> outcome;
    std::exception_ptr failure;
    try
    {
        outcome = co_await start_runtime(context);
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    if (failure || !outcome)
    {
        /**
         * Startup owns partial resources even before maintenance registration.
         * Retain the original failure if cleanup cannot finish; the pending
         * state prevents another start until the caller retries cleanup.
         */
        cleanup_pending_ = true;
        try
        {
            (void)co_await stop(context);
        }
        catch (...)
        {
            // Cleanup remains pending; do not replace the startup failure.
        }
    }
    if (failure)
        std::rethrow_exception(failure);
    co_return outcome;
}

auto mongodb_service::start_runtime(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (rebuild_pool_)
    {
        auto replacement = std::make_shared<mongodb::connection_pool>(pool_->context(), options_);
        pool_ = std::move(replacement);
        rebuild_pool_ = false;
    }
    auto warmed = co_await with_deadline(context.io, context.operation_deadline,
        warm_pool(*pool_, context.cancellation), context.cancellation);
    if (!warmed)
        co_return std::unexpected(warmed.error());
    if (context.cancellation.is_cancelled() || context.operation_deadline.expired())
    {
        /**
         * A successful late handshake still owns resources, but must not admit
         * background work after startup cancellation. Preserve the admission
         * failure while attempting cleanup with the original remaining budget.
         */
        const auto failure = std::make_error_code(context.cancellation.is_cancelled()
                ? std::errc::operation_canceled
                : std::errc::timed_out);
        co_return std::unexpected(failure);
    }
    maintenance_stop_ = std::stop_source{};
    auto supervised = context.supervisor.supervise(
        std::format("mongodb-maintenance:{}", instance_),
        [pool = pool_, stop = maintenance_stop_.get_token()](cancel_token&) -> task<std::expected<void, std::error_code>>
        {
            co_await pool->run_maintenance(stop);
            co_return {};
        },
        recovery_, requirement_ == service_requirement::required,
        [stop = maintenance_stop_]() mutable noexcept
        {
            stop.request_stop();
        });
    if (!supervised)
        co_return std::unexpected(supervised.error());
    started_ = true;
    co_return {};
}

auto mongodb_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    cleanup_pending_ = true;
    maintenance_stop_.request_stop();
    co_await pool_->async_close();
    const auto maintenance_name = std::format("mongodb-maintenance:{}", instance_);
    auto maintenance_pending = [&]() noexcept
    {
        const auto state = context.supervisor.state(maintenance_name);
        return state && *state != supervised_task_state::stopped && *state != supervised_task_state::failed;
    };
    /**
     * Closing admission does not release borrower-owned transports. Keep the
     * service registered until leases return, or report an unfinished cleanup
     * that the lifecycle coordinator can retry with a fresh deadline.
     */
    while (pool_->checked_out_count() != 0 || pool_->connecting_count() != 0 || maintenance_pending())
    {
        if (context.cancellation.is_cancelled())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        if (context.operation_deadline.expired())
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        const auto waited = co_await async_timer_wait(context.io,
            std::min(context.operation_deadline.remaining(),
                std::chrono::duration_cast<deadline::duration>(std::chrono::milliseconds{1})),
            context.cancellation);
        if (!waited)
        {
            if (context.cancellation.is_cancelled())
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            co_return std::unexpected(waited.error());
        }
    }
    started_ = false;
    cleanup_pending_ = false;
    rebuild_pool_ = true;
    co_return {};
}

auto mongodb_service::probe(service_context& context) -> task<health_report>
{
    if (cleanup_pending_)
        co_return health_report{
            .status = service_health::stopping,
            .message = "mongodb cleanup pending",
        };
    /**
     * Reject expired health work before it claims a pool lease or sends a ping.
     * Cancellation is local to this probe and does not retire healthy slots.
     */
    if (context.cancellation.is_cancelled() || context.operation_deadline.expired())
        co_return health_report{
            .status = service_health::down,
            .message = "mongodb health check not admitted",
            .error = std::make_error_code(context.cancellation.is_cancelled()
                    ? std::errc::operation_canceled
                    : std::errc::timed_out),
        };
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "mongodb pool stopped"};
    auto result = co_await with_deadline(context.io, context.operation_deadline,
        probe_pool(*pool_, context.cancellation), context.cancellation);
    co_return health_report{
        .status = result ? service_health::up : service_health::down,
        .message = result ? "mongodb PING succeeded" : "mongodb PING failed",
        .error = result ? std::error_code{} : result.error(),
    };
}

auto auto_configure_mongodb(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port", "username", "password", "database", "minimum_size", "maximum_size"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    mongodb::connection_pool_options options;
    if (!pool_size_properties_are_valid(configuration.properties, options.minimum_size, options.maximum_size))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        const auto& value = configuration.properties;
        options.connection.host = value.value("host", options.connection.host);
        options.connection.port = value.value("port", options.connection.port);
        options.connection.username = value.value("username",
            options.connection.username);
        options.connection.password = value.value("password",
            options.connection.password);
        options.connection.database = value.value("database",
            options.connection.database);
        options.minimum_size = value.value("minimum_size", options.minimum_size);
        options.maximum_size = value.value("maximum_size", options.maximum_size);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    auto service = std::make_shared<mongodb_service>(context.io,
        std::move(options), configuration.instance,
        configuration.requirement, configuration.recovery);
    return context.services.add_managed_named<mongodb_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
