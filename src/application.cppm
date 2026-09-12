module;

#include <cnetmod/config.hpp>

/// Batteries-included cnetmod application framework.
export module cnetmod.application;

export import cnetmod.application.configuration;
export import cnetmod.application.lifecycle;
export import cnetmod.application.service_registry;
export import cnetmod.application.http;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
export import cnetmod.application.redis;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
export import cnetmod.application.mysql;
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
