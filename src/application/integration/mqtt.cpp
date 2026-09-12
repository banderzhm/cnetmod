module cnetmod.application.mqtt;

#ifdef CNETMOD_HAS_PROTOCOL_MQTT
import std;
import cnetmod.core.log;

namespace cnetmod::application {

mqtt_service::mqtt_service(io_context& context,
    mqtt::connect_options connection, mqtt::reconnect_options reconnect)
    : connection_(std::move(connection)), client_(context)
{
    client_.set_reconnect(std::move(reconnect));
}

auto mqtt_service::client() noexcept -> mqtt::client&
{
    return client_;
}

auto mqtt_service::start() -> task<std::expected<void, std::error_code>>
{
    if (started_)
        co_return {};
    auto connected = co_await client_.connect(connection_);
    if (!connected)
    {
        logger::error("MQTT startup failed: {}", connected.error());
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_refused));
    }
    started_ = true;
    co_return {};
}

auto mqtt_service::stop() -> task<std::expected<void, std::error_code>>
{
    if (started_ && client_.is_connected())
    {
        auto disconnected = co_await client_.disconnect();
        if (!disconnected)
            logger::warn("MQTT shutdown failed: {}", disconnected.error());
    }
    client_.close();
    started_ = false;
    co_return {};
}

auto install_mqtt(http_application& application,
    mqtt::connect_options connection, mqtt::reconnect_options reconnect)
    -> mqtt_service&
{
    if (application.services().find<mqtt_service>())
        throw std::logic_error("MQTT is already installed");
    auto service = std::make_shared<mqtt_service>(application.context(),
        std::move(connection), std::move(reconnect));
    auto& result = *service;
    application.services().add<mqtt_service>(service);
    application.lifecycle().on_start([service]
        {
            return service->start();
        });
    application.lifecycle().on_stop([service]
        {
            return service->stop();
        });
    return result;
}

} // namespace cnetmod::application
#endif
