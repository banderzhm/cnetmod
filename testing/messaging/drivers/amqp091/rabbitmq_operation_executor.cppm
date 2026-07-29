module;
#include <cnetmod/config.hpp>

export module cnetmod.testing.messaging.amqp091_driver:rabbitmq_operation_executor;

import std;
import nlohmann.json;
import cnetmod.io.io_context;
import cnetmod.coro.task;

export namespace cnetmod::testing::messaging::amqp091_driver {

/// Executes one JSON contract operation against a real AMQP 0-9-1 broker.
auto execute_rabbitmq_operation(io_context& context,
    const nlohmann::json& request)
    -> task<nlohmann::json>;

} // namespace cnetmod::testing::messaging::amqp091_driver
