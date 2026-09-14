module;

#include <cnetmod/config.hpp>

/**
 * @brief Batteries-included cnetmod application framework.
 */
export module cnetmod.application;

export import cnetmod.application.configuration;
export import cnetmod.application.managed_service;
export import cnetmod.application.recovery_policy;
export import cnetmod.application.service_registry;
export import cnetmod.application.task_supervisor;
export import cnetmod.application.health_registry;
export import cnetmod.application.service_lifecycle;
export import cnetmod.application.auto_configuration;
export import cnetmod.application.host;
export import cnetmod.application.http_client;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
export import cnetmod.application.redis;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
export import cnetmod.application.mysql;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
export import cnetmod.application.postgresql;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
export import cnetmod.application.mongodb;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
export import cnetmod.application.kafka;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
export import cnetmod.application.mqtt;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
export import cnetmod.application.amqp091;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
export import cnetmod.application.amqp10;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
export import cnetmod.application.openai;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_GRPC
export import cnetmod.application.grpc;
#endif
