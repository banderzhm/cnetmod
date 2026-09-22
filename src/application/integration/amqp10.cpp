module cnetmod.application.amqp10;
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import std;
import cnetmod.json;
namespace cnetmod::application {
amqp10_service::amqp10_service(io_context& io, amqp10::client_options options,
    std::string instance, service_requirement requirement, recovery_policy recovery)
    : options_(std::move(options)), client_(io), instance_(std::move(instance)),
      requirement_(requirement), recovery_(recovery) {}
auto amqp10_service::client() noexcept -> amqp10::client& { return client_; }
auto amqp10_service::key() const -> service_key { return {"amqp10", instance_}; }
auto amqp10_service::requirement() const noexcept -> service_requirement { return requirement_; }
auto amqp10_service::recovery() const noexcept -> recovery_policy { return recovery_; }
auto amqp10_service::start(service_context& context) -> task<std::expected<void, std::error_code>>
{
    auto connected = co_await client_.connect(options_, context.cancellation);
    if (!connected) co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
    co_return {};
}
auto amqp10_service::stop(service_context& context) -> task<std::expected<void, std::error_code>>
{
    auto closed = co_await client_.close(context.cancellation);
    if (!closed) co_return std::unexpected(std::make_error_code(std::errc::io_error));
    co_return {};
}
auto amqp10_service::probe(service_context&) -> task<health_report>
{
    const auto up = client_.state() == amqp10::connection_state::opened;
    co_return health_report{.status = up ? service_health::up : service_health::down,
        .message = up ? "amqp 1.0 connected" : "amqp 1.0 disconnected"};
}
auto auto_configure_amqp10(const configured_service& configuration,
    auto_configuration_context& context) -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port",
            "username", "password", "container_id"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        amqp10::client_options options;
        options.endpoint.host = cnetmod::json::value_or(
            configuration.properties, "host", options.endpoint.host);
        options.endpoint.port = cnetmod::json::value_or(
            configuration.properties, "port", options.endpoint.port);
        options.credentials.username = cnetmod::json::value_or(
            configuration.properties, "username", std::string{});
        options.credentials.password = cnetmod::json::value_or(
            configuration.properties, "password", std::string{});
        options.container_id = cnetmod::json::value_or(
            configuration.properties, "container_id", std::string{"cnetmod"});
        auto service = std::make_shared<amqp10_service>(context.io, std::move(options),
            configuration.instance, configuration.requirement, configuration.recovery);
        return context.services.add_managed_named<amqp10_service>(
            configuration.instance, std::move(service));
    }
    catch (...) { return std::unexpected(std::make_error_code(std::errc::invalid_argument)); }
}
} // namespace cnetmod::application
#endif
