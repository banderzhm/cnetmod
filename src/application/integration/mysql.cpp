module cnetmod.application.mysql;

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import std;
import cnetmod.json;
import cnetmod.application.task_supervisor;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

namespace cnetmod::application {

namespace {

    auto ping_connection(mysql::client& connection, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto response = co_await connection.ping(cancellation);
        if (!response.is_err())
            co_return {};
        auto error = connection.last_error();
        co_return std::unexpected(error ? error : std::make_error_code(std::errc::io_error));
    }

    auto verify_shard(mysql::sharded_connection_pool& pool,
        io_context& event_loop, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto connection = co_await pool.async_get_connection(
            event_loop, cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());
        co_return {};
    }

    auto probe_shard(mysql::sharded_connection_pool& pool,
        io_context& event_loop, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto connection = co_await pool.async_get_connection(
            event_loop, cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());
        co_return co_await ping_connection(connection->get(), cancellation);
    }

} // namespace

mysql_service::mysql_service(io_context& io, mysql::pool_params options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery)
    : io_(io), pool_(std::make_unique<mysql::connection_pool>(io, std::move(options))), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery)
{
}

mysql_service::mysql_service(io_context& control,
    std::span<io_context* const> event_loops, mysql::pool_params options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery)
    : io_(control),
      sharded_pool_(std::make_unique<mysql::sharded_connection_pool>(
          std::vector<io_context*>{event_loops.begin(), event_loops.end()},
          std::move(options))),
      event_loops_(event_loops.begin(), event_loops.end()),
      instance_(std::move(instance)), requirement_(requirement),
      recovery_(recovery)
{
}

auto mysql_service::pool() -> mysql::connection_pool&
{
    if (!pool_)
        throw std::logic_error{
            "mysql_service::pool() is unavailable in multi-loop mode; use acquire()"};
    return *pool_;
}

auto mysql_service::acquire(cancel_token& cancellation)
    -> task<std::expected<mysql::pooled_connection, std::error_code>>
{
    if (pool_)
        co_return co_await pool_->async_get_connection(cancellation);
    auto* caller = io_context::current();
    if (caller == nullptr)
        co_return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));
    co_return co_await sharded_pool_->async_get_connection(
        *caller, cancellation);
}

auto mysql_service::acquire()
    -> task<std::expected<mysql::pooled_connection, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await acquire(cancellation);
}

auto mysql_service::key() const -> service_key
{
    return {"mysql", instance_};
}

auto mysql_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto mysql_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto mysql_service::shutdown_required() const noexcept -> bool
{
    return started_;
}

auto mysql_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    if (!started_)
    {
        auto supervised = context.supervisor.supervise(
            std::format("mysql-pool:{}", instance_),
            [this](cancel_token&) -> task<std::expected<void, std::error_code>>
            {
                if (pool_)
                    co_await pool_->async_run();
                else
                    co_await resume_on(io_, sharded_pool_->async_run());
                co_return {};
            },
            recovery_, requirement_ == service_requirement::required,
            [this]() noexcept
            {
                if (pool_)
                    pool_->request_stop();
                else
                    sharded_pool_->request_stop();
            });
        if (!supervised)
            co_return std::unexpected(supervised.error());
        started_ = true;
    }
    std::expected<void, std::error_code> verified;
    if (pool_)
    {
        auto connection = co_await with_deadline(context.io,
            context.operation_deadline, acquire(context.cancellation),
            context.cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());
    }
    else
    {
        verified = co_await with_deadline(context.io,
            context.operation_deadline,
            resume_on(context.io, starts_on(*event_loops_.front(),
                verify_shard(*sharded_pool_, *event_loops_.front(),
                    context.cancellation))),
            context.cancellation);
        if (!verified)
            co_return std::unexpected(verified.error());
    }
    co_return {};
}

auto mysql_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (started_)
    {
        if (pool_)
            co_await pool_->cancel();
        else
            co_await sharded_pool_->cancel();
    }
    bool waited_for_leases = false;
    while ((pool_ ? pool_->checked_out_count()
                  : sharded_pool_->checked_out_count()) != 0)
    {
        waited_for_leases = true;
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
    if (waited_for_leases)
    {
        if (pool_)
            co_await pool_->cancel();
        else
            co_await sharded_pool_->cancel();
    }
    started_ = false;
    co_return {};
}

auto mysql_service::probe(service_context& context) -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "mysql pool stopped"};
    std::expected<void, std::error_code> pong;
    if (pool_)
    {
        auto connection = co_await with_deadline(context.io,
            context.operation_deadline, acquire(context.cancellation),
            context.cancellation);
        if (!connection)
            co_return health_report{.status = service_health::down,
                .message = "mysql connection unavailable",
                .error = connection.error()};
        pong = co_await with_deadline(context.io,
            context.operation_deadline,
            ping_connection(connection->get(), context.cancellation),
            context.cancellation);
    }
    else
    {
        pong = co_await with_deadline(context.io, context.operation_deadline,
            resume_on(context.io, starts_on(*event_loops_.front(),
                probe_shard(*sharded_pool_, *event_loops_.front(),
                    context.cancellation))),
            context.cancellation);
    }
    co_return health_report{
        .status = pong ? service_health::up : service_health::down,
        .message = pong ? "mysql PING succeeded" : "mysql PING failed",
        .error = pong ? std::error_code{} : pong.error(),
    };
}

auto auto_configure_mysql(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port", "username", "password", "database", "minimum_size", "maximum_size", "ssl", "tls_verify", "tls_ca_file", "connect_timeout_ms", "pool_timeout_ms", "retry_interval_ms", "ping_interval_ms", "ping_timeout_ms"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    constexpr auto maximum_timeout_ms = std::int64_t{86'400'000};
    for (const auto name : {"connect_timeout_ms", "pool_timeout_ms",
             "retry_interval_ms", "ping_interval_ms", "ping_timeout_ms"})
    {
        if (!integer_property_in_range(configuration.properties, name, 1,
                maximum_timeout_ms))
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
    }
    mysql::pool_params options;
    if (!pool_size_properties_are_valid(configuration.properties, options.initial_size, options.max_size))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        const auto& value = configuration.properties;
        options.host = cnetmod::json::value_or(value, "host", options.host);
        options.port = cnetmod::json::value_or(value, "port", options.port);
        options.username = cnetmod::json::value_or(value, "username", options.username);
        options.password = cnetmod::json::value_or(value, "password", options.password);
        options.database = cnetmod::json::value_or(value, "database", options.database);
        options.initial_size = cnetmod::json::value_or(value, "minimum_size", options.initial_size);
        options.max_size = cnetmod::json::value_or(value, "maximum_size", options.max_size);
        if (value.contains("ssl"))
        {
            const auto mode = value.at("ssl").get<std::string>();
            if (mode == "disable")
                options.ssl = mysql::ssl_mode::disable;
            else if (mode == "enable")
                options.ssl = mysql::ssl_mode::enable;
            else if (mode == "require")
                options.ssl = mysql::ssl_mode::require;
            else
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
        }
        options.tls_verify = cnetmod::json::value_or(value, "tls_verify", options.tls_verify);
        options.tls_ca_file = cnetmod::json::value_or(value, "tls_ca_file", options.tls_ca_file);
        const auto duration = [&value](std::string_view name,
                                  std::chrono::steady_clock::duration fallback)
        {
            if (!value.contains(name))
                return fallback;
            return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds{value.at(name).as<std::int64_t>()});
        };
        options.connect_timeout = duration("connect_timeout_ms", options.connect_timeout);
        options.pool_timeout = duration("pool_timeout_ms", options.pool_timeout);
        options.retry_interval = duration("retry_interval_ms", options.retry_interval);
        options.ping_interval = duration("ping_interval_ms", options.ping_interval);
        options.ping_timeout = duration("ping_timeout_ms", options.ping_timeout);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    if (options.username.empty() || options.database.empty())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    std::shared_ptr<mysql_service> service;
    if (context.event_loops.size() > 1)
        service = std::make_shared<mysql_service>(context.io,
            context.event_loops, std::move(options), configuration.instance,
            configuration.requirement, configuration.recovery);
    else
        service = std::make_shared<mysql_service>(context.io,
            std::move(options), configuration.instance,
            configuration.requirement, configuration.recovery);
    return context.services.add_managed_named<mysql_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
