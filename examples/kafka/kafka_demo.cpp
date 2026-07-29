#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.kafka;

#include "kafka_config.hpp"
#include "producer_service.hpp"
#include "consumer_service.hpp"
#include "kafka_application.hpp"

auto main() -> int
{
    namespace cn = cnetmod;
    auto config = kafka_example::configuration::from_environment();
    logger::info(
        "Kafka service starting: broker={}:{} topic={} producers={} consumers={} messages={}",
        config.host, config.port, config.topic, config.producer_concurrency,
        config.consumer_concurrency, config.message_count);

    cn::net_init network;
    auto context = cn::make_io_context();
    kafka_example::application app(*context, std::move(config));
    cn::spawn(*context, app.start());
    context->run();
}
