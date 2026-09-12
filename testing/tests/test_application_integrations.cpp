#include "test_framework.hpp"

#include <cnetmod/config.hpp>

import std;
import cnetmod.application;

namespace application = cnetmod::application;

namespace {

auto make_application() -> std::unique_ptr<application::http_application>
{
    return std::make_unique<application::http_application>(
        application::application_options{
            .name = "application-integration-test",
            .logging = {.manage_lifecycle = false},
            .http = {.address = "127.0.0.1", .port = 0, .access_logging = false},
            .management = {.enabled = false},
            .install_signal_handlers = false,
        });
}

} // namespace

TEST(application_registers_enabled_data_and_messaging_services)
{
    auto app = make_application();

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
    auto& redis = application::install_redis(*app);
    ASSERT_TRUE(&app->services().require<application::redis_service>() ==
        &redis);
    bool duplicate_rejected = false;
    try
    {
        (void)application::install_redis(*app);
    }
    catch (const std::logic_error&)
    {
        duplicate_rejected = true;
    }
    ASSERT_TRUE(duplicate_rejected);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
    auto& mysql = application::install_mysql(*app);
    ASSERT_TRUE(&app->services().require<application::mysql_service>() ==
        &mysql);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
    auto& kafka = application::install_kafka(*app);
    ASSERT_TRUE(&app->services().require<application::kafka_service>() ==
        &kafka);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
    auto& mqtt = application::install_mqtt(*app);
    ASSERT_TRUE(&app->services().require<application::mqtt_service>() ==
        &mqtt);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    auto& amqp091 = application::install_amqp091(*app);
    ASSERT_TRUE(&app->services().require<application::amqp091_service>() ==
        &amqp091);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
    auto& amqp10 = application::install_amqp10(*app);
    ASSERT_TRUE(&app->services().require<application::amqp10_service>() ==
        &amqp10);
#endif
}

RUN_TESTS();
