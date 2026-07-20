module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc;

export import cnetmod.protocol.grpc.types;
export import cnetmod.protocol.grpc.codec;
export import cnetmod.protocol.grpc.streaming;
export import cnetmod.protocol.grpc.proto;
export import cnetmod.protocol.grpc.server;
export import cnetmod.protocol.grpc.client;
export import cnetmod.protocol.grpc.health;
export import cnetmod.protocol.grpc.reflection;
export import cnetmod.protocol.grpc.governance.endpoint;
export import cnetmod.protocol.grpc.governance.discovery;
export import cnetmod.protocol.grpc.governance.load_balancer;
export import cnetmod.protocol.grpc.governance.retry;
export import cnetmod.protocol.grpc.governance.circuit_breaker;
export import cnetmod.protocol.grpc.governance.admission;
export import cnetmod.protocol.grpc.governance.observability;
export import cnetmod.protocol.grpc.governance.server_policy;
export import cnetmod.protocol.grpc.governance.client;
