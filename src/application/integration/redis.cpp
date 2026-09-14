module cnetmod.application.redis;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import std;
import cnetmod.application.task_supervisor;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

namespace cnetmod::application {

redis_service::redis_service(io_context& io, redis::pool_params options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery)
    : io_(io), pool_(io, std::move(options)), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery)
{
}

auto redis_service::pool() noexcept -> redis::connection_pool&
{
    return pool_;
}

auto redis_service::key() const -> service_key
{
    return {"redis", instance_};
}

auto redis_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto redis_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto redis_service::shutdown_required() const noexcept -> bool
{
    return started_;
}

auto redis_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    if (!started_)
    {
        auto supervised = context.supervisor.supervise(
            std::format("redis-pool:{}", instance_),
            [this](cancel_token&) -> task<std::expected<void, std::error_code>>
            {
                co_await pool_.async_run();
                co_return {};
            },
            recovery_, requirement_ == service_requirement::required,
            [this]() noexcept
            {
                pool_.request_stop();
            });
        if (!supervised)
            co_return std::unexpected(supervised.error());
        started_ = true;
    }
    auto connection = co_await pool_.async_get_connection(
        context.cancellation);
    if (!connection)
        co_return std::unexpected(connection.error());
    co_return {};
}

auto redis_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (started_)
        co_await pool_.cancel();
    while (pool_.checked_out_count() != 0)
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
            co_return std::unexpected(waited.error());
    }
    started_ = false;
    co_return {};
}

auto redis_service::probe(service_context& context) -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "redis pool stopped"};
    auto connection = co_await with_deadline(context.io, context.operation_deadline,
        pool_.async_get_connection(context.cancellation), context.cancellation);
    if (!connection)
        co_return health_report{.status = service_health::down,
            .message = "redis connection unavailable",
            .error = connection.error()};
    auto pong = co_await with_deadline(context.io, context.operation_deadline,
        connection->get().ping(context.cancellation), context.cancellation);
    co_return health_report{
        .status = pong ? service_health::up : service_health::down,
        .message = pong ? "redis PING succeeded" : "redis PING failed",
        .error = pong ? std::error_code{} : pong.error(),
    };
}

auto auto_configure_redis(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port", "username", "password", "database", "minimum_size", "maximum_size", "tls"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    redis::pool_params options;
    if (!pool_size_properties_are_valid(configuration.properties, options.initial_size, options.max_size))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        const auto& value = configuration.properties;
        options.host = value.value("host", options.host);
        options.port = value.value("port", options.port);
        options.username = value.value("username", options.username);
        options.password = value.value("password", options.password);
        options.db = value.value("database", options.db);
        options.initial_size = value.value("minimum_size", options.initial_size);
        options.max_size = value.value("maximum_size", options.max_size);
        options.tls = value.value("tls", options.tls);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    auto service = std::make_shared<redis_service>(context.io,
        std::move(options), configuration.instance,
        configuration.requirement, configuration.recovery);
    return context.services.add_managed_named<redis_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
