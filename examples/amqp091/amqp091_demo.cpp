#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.amqp091;

#include "amqp091_config.hpp"
#include "publisher_service.hpp"
#include "listener_container.hpp"
#include "amqp091_application.hpp"

auto main() -> int
{
    namespace cn = cnetmod;
    auto config = amqp091_example::configuration::from_environment();
    logger::info(
        "AMQP 0-9-1 service starting: broker={}:{} publishers={} consumers={} messages={}",
        config.host, config.port, config.publisher_concurrency,
        config.consumer_concurrency, config.message_count);
    cn::net_init network;
    auto context = cn::make_io_context();
    amqp091_example::application app(*context, std::move(config));
    cn::spawn(*context, app.start());
    context->run();
}
