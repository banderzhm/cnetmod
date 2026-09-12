module;

#include <cnetmod/config.hpp>

/// Application-managed MQTT client.
export module cnetmod.application.mqtt;

#ifdef CNETMOD_HAS_PROTOCOL_MQTT
import std;
import cnetmod.application.http;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.mqtt;

namespace cnetmod::application {

export class mqtt_service final
{
public:
    mqtt_service(io_context& context, mqtt::connect_options connection,
        mqtt::reconnect_options reconnect = {});

    [[nodiscard]] auto client() noexcept -> mqtt::client&;
    [[nodiscard]] auto start() -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto stop() -> task<std::expected<void, std::error_code>>;

private:
    mqtt::connect_options connection_;
    mqtt::client client_;
    bool started_ = false;
};

/// Connect MQTT during startup and disconnect it during shutdown.
export auto install_mqtt(http_application& application,
    mqtt::connect_options connection = {},
    mqtt::reconnect_options reconnect = {}) -> mqtt_service&;

} // namespace cnetmod::application
#endif
