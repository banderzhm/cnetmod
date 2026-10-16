module;

#include <cnetmod/config.hpp>

module cnetmod.application.auto_configuration;

import cnetmod.application.http_client;
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import cnetmod.application.openai;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import cnetmod.application.redis;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import cnetmod.application.mysql;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
import cnetmod.application.postgresql;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import cnetmod.application.mongodb;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import cnetmod.application.kafka;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
import cnetmod.application.mqtt;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import cnetmod.application.amqp091;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import cnetmod.application.amqp10;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import cnetmod.application.grpc;
#endif

namespace cnetmod::application {

void register_builtin_auto_configurations(
    auto_configuration_registry& registry)
{
    registry.add("http_client", auto_configure_http_client,
        integration_loop_mode::loop_local);
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
    registry.add("openai", auto_configure_openai,
        integration_loop_mode::owned);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
    registry.add("redis", auto_configure_redis,
        integration_loop_mode::loop_local);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
    registry.add("mysql", auto_configure_mysql,
        integration_loop_mode::loop_local);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
    registry.add("postgresql", auto_configure_postgresql,
        integration_loop_mode::loop_local);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
    registry.add("mongodb", auto_configure_mongodb);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
    registry.add("kafka", auto_configure_kafka);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
    registry.add("mqtt", auto_configure_mqtt);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    registry.add("amqp091", auto_configure_amqp091);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
    registry.add("amqp10", auto_configure_amqp10);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_GRPC
    registry.add("grpc", auto_configure_grpc_client);
    registry.add("grpc_server", auto_configure_grpc_server);
#endif
}

} // namespace cnetmod::application
