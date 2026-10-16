module cnetmod.application.postgresql;

#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
import std;
import cnetmod.json;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

namespace cnetmod::application {

struct postgresql_service::pool_shard
{
    pool_shard(io_context& event_loop,
        const postgresql::connection_pool_options& options)
        : event_loop(&event_loop), pool(event_loop, options)
    {
    }

    io_context* event_loop;
    postgresql::connection_pool pool;
};

postgresql_service::~postgresql_service() = default;

namespace {

    /**
     * @brief Checks the server without exposing SQL diagnostics to health output.
     */
    auto probe_connection(postgresql::client& connection, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto response = co_await connection.query("SELECT 1", cancellation);
        if (!response.is_err())
            co_return {};
        const auto error = connection.last_error();
        co_return std::unexpected(error ? error : std::make_error_code(std::errc::io_error));
    }

} // namespace

postgresql_service::postgresql_service(io_context& io,
    postgresql::connection_pool_options options, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : instance_(std::move(instance)), requirement_(requirement),
      recovery_(recovery)
{
    pools_.push_back(std::make_unique<pool_shard>(io, options));
}

postgresql_service::postgresql_service(
    std::span<io_context* const> event_loops,
    postgresql::connection_pool_options options, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : instance_(std::move(instance)), requirement_(requirement),
      recovery_(recovery)
{
    if (event_loops.empty() ||
        options.maximum_connections < event_loops.size())
        throw std::invalid_argument{
            "PostgreSQL maximum_connections must cover every event loop"};
    pools_.reserve(event_loops.size());
    const auto minimum_quotient =
        options.minimum_connections / event_loops.size();
    const auto minimum_remainder =
        options.minimum_connections % event_loops.size();
    const auto maximum_quotient =
        options.maximum_connections / event_loops.size();
    const auto maximum_remainder =
        options.maximum_connections % event_loops.size();
    for (std::size_t index = 0; index < event_loops.size(); ++index)
    {
        auto shard_options = options;
        shard_options.minimum_connections = minimum_quotient +
            (index < minimum_remainder ? 1U : 0U);
        shard_options.maximum_connections = maximum_quotient +
            (index < maximum_remainder ? 1U : 0U);
        pools_.push_back(
            std::make_unique<pool_shard>(*event_loops[index], shard_options));
    }
}

auto postgresql_service::pool() -> postgresql::connection_pool&
{
    auto* current = io_context::current();
    for (auto& shard : pools_)
        if (shard->event_loop == current)
            return shard->pool;
    throw std::logic_error{
        "PostgreSQL pool requested outside an owning event loop"};
}

auto postgresql_service::key() const -> service_key
{
    return {"postgresql", instance_};
}

auto postgresql_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto postgresql_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto postgresql_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    if (started_ && std::ranges::none_of(pools_, [](const auto& shard)
            { return static_cast<bool>(shard->pool.background_error()); }))
        co_return {};
    for (auto& shard : pools_)
    {
        auto result = co_await with_deadline(context.io,
            context.operation_deadline,
            resume_on(context.io, starts_on(*shard->event_loop,
                shard->pool.warm_up(context.cancellation))),
            context.cancellation);
        if (!result)
            co_return std::unexpected(result.error());
    }
    started_ = true;
    co_return {};
}

auto postgresql_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (!started_ && std::ranges::all_of(pools_, [](const auto& shard)
            { return shard->pool.size() == 0; }))
        co_return {};
    for (auto& shard : pools_)
    {
        auto result = co_await with_deadline(context.io,
            context.operation_deadline,
            resume_on(context.io, starts_on(*shard->event_loop,
                shard->pool.close(context.cancellation))),
            context.cancellation);
        if (!result)
            co_return std::unexpected(result.error());
    }
    started_ = false;
    co_return {};
}

auto postgresql_service::probe(service_context& context) -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "postgresql pool stopped"};
    std::expected<void, std::error_code> result;
    for (auto& shard : pools_)
    {
        if (const auto failure = shard->pool.background_error())
            co_return health_report{.status = service_health::down,
                .message = "postgresql background reconnect failed",
                .error = failure};
        auto probe = [&]() -> task<std::expected<void, std::error_code>>
        {
            auto connection = co_await shard->pool.acquire(
                context.cancellation);
            if (!connection)
                co_return std::unexpected(connection.error());
            auto checked = co_await probe_connection(connection->get(),
                context.cancellation);
            if (!checked)
                connection->discard();
            co_return checked;
        };
        result = co_await with_deadline(context.io,
            context.operation_deadline,
            resume_on(context.io,
                starts_on(*shard->event_loop, probe())),
            context.cancellation);
        if (!result)
            break;
    }
    co_return health_report{
        .status = result ? service_health::up : service_health::down,
        .message = result ? "postgresql query succeeded" : "postgresql query failed",
        .error = result ? std::error_code{} : result.error(),
    };
}

auto auto_configure_postgresql(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port", "username", "password", "database", "minimum_size", "maximum_size"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    postgresql::connection_pool_options options;
    if (!pool_size_properties_are_valid(configuration.properties, options.minimum_connections, options.maximum_connections))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        const auto& value = configuration.properties;
        options.connection.host = cnetmod::json::value_or(value, "host", options.connection.host);
        options.connection.port = cnetmod::json::value_or(value, "port", options.connection.port);
        options.connection.username = cnetmod::json::value_or(value, "username",
            options.connection.username);
        options.connection.password = cnetmod::json::value_or(value, "password",
            options.connection.password);
        options.connection.database = cnetmod::json::value_or(value, "database",
            options.connection.database);
        options.minimum_connections = cnetmod::json::value_or(value, "minimum_size",
            options.minimum_connections);
        options.maximum_connections = cnetmod::json::value_or(value, "maximum_size",
            options.maximum_connections);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    if (context.event_loops.size() > 1 &&
        options.maximum_connections < context.event_loops.size())
        return std::unexpected(std::make_error_code(
            std::errc::invalid_argument));
    std::shared_ptr<postgresql_service> service;
    if (context.event_loops.size() > 1)
        service = std::make_shared<postgresql_service>(context.event_loops,
            std::move(options), configuration.instance,
            configuration.requirement, configuration.recovery);
    else
        service = std::make_shared<postgresql_service>(context.io,
            std::move(options), configuration.instance,
            configuration.requirement, configuration.recovery);
    return context.services.add_managed_named<postgresql_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
