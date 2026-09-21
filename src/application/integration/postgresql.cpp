module cnetmod.application.postgresql;

#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
import std;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

namespace cnetmod::application {
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
    : pool_(io, std::move(options)), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery)
{
}

auto postgresql_service::pool() noexcept -> postgresql::connection_pool&
{
    return pool_;
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
    if (started_ && !pool_.background_error())
        co_return {};
    const auto result = co_await with_deadline(context.io, context.operation_deadline,
        pool_.warm_up(context.cancellation), context.cancellation);
    if (!result)
        co_return std::unexpected(result.error());
    started_ = true;
    co_return {};
}

auto postgresql_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (!started_ && pool_.size() == 0)
        co_return {};
    auto result = co_await with_deadline(context.io, context.operation_deadline,
        pool_.close(context.cancellation), context.cancellation);
    if (!result)
        co_return std::unexpected(result.error());
    started_ = false;
    co_return {};
}

auto postgresql_service::probe(service_context& context) -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "postgresql pool stopped"};
    if (const auto failure = pool_.background_error())
        co_return health_report{.status = service_health::down,
            .message = "postgresql background reconnect failed",
            .error = failure};
    auto connection = co_await with_deadline(context.io, context.operation_deadline,
        pool_.acquire(context.cancellation), context.cancellation);
    if (!connection)
        co_return health_report{.status = service_health::down,
            .message = "postgresql connection unavailable",
            .error = connection.error()};
    auto result = co_await with_deadline(context.io, context.operation_deadline,
        probe_connection(connection->get(), context.cancellation), context.cancellation);
    if (!result)
        connection->discard();
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
        options.connection.host = value.value("host", options.connection.host);
        options.connection.port = value.value("port", options.connection.port);
        options.connection.username = value.value("username",
            options.connection.username);
        options.connection.password = value.value("password",
            options.connection.password);
        options.connection.database = value.value("database",
            options.connection.database);
        options.minimum_connections = value.value("minimum_size",
            options.minimum_connections);
        options.maximum_connections = value.value("maximum_size",
            options.maximum_connections);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    auto service = std::make_shared<postgresql_service>(context.io,
        std::move(options), configuration.instance,
        configuration.requirement, configuration.recovery);
    return context.services.add_managed_named<postgresql_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
