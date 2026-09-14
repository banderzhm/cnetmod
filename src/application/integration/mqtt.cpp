module cnetmod.application.mqtt;
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
import std;
namespace cnetmod::application {
mqtt_service::mqtt_service(io_context& io, mqtt::connect_options connection,
    mqtt::reconnect_options reconnect, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : connection_(std::move(connection)), client_(io), instance_(std::move(instance)),
      requirement_(requirement), recovery_(recovery)
{
    client_.set_reconnect(std::move(reconnect));
}
auto mqtt_service::client() noexcept -> mqtt::client& { return client_; }
auto mqtt_service::key() const -> service_key { return {"mqtt", instance_}; }
auto mqtt_service::requirement() const noexcept -> service_requirement { return requirement_; }
auto mqtt_service::recovery() const noexcept -> recovery_policy { return recovery_; }
auto mqtt_service::start(service_context&) -> task<std::expected<void, std::error_code>>
{
    if (client_.is_connected()) co_return {};
    auto result = co_await client_.connect(connection_);
    if (!result)
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_refused));
    co_return {};
}
auto mqtt_service::stop(service_context&) -> task<std::expected<void, std::error_code>>
{
    if (client_.is_connected()) (void)co_await client_.disconnect();
    client_.close();
    co_return {};
}
auto mqtt_service::probe(service_context&) -> task<health_report>
{
    co_return health_report{.status = client_.is_connected() ? service_health::up : service_health::down,
        .message = client_.is_connected() ? "mqtt connected" : "mqtt disconnected"};
}
auto auto_configure_mqtt(const configured_service& configuration,
    auto_configuration_context& context) -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port",
            "client_id", "username", "password", "tls"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        mqtt::connect_options options;
        options.host = configuration.properties.value("host", options.host);
        options.port = configuration.properties.value("port", options.port);
        options.client_id = configuration.properties.value("client_id", options.client_id);
        options.username = configuration.properties.value("username", options.username);
        options.password = configuration.properties.value("password", options.password);
        options.tls = configuration.properties.value("tls", options.tls);
        auto service = std::make_shared<mqtt_service>(context.io, std::move(options),
            mqtt::reconnect_options{}, configuration.instance,
            configuration.requirement, configuration.recovery);
        return context.services.add_managed_named<mqtt_service>(
            configuration.instance, std::move(service));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
}
} // namespace cnetmod::application
#endif
