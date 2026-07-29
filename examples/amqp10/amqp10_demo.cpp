#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.amqp10;

#include "amqp10_config.hpp"
#include "sender_service.hpp"
#include "receiver_container.hpp"
#include "amqp10_application.hpp"

auto main() -> int
{
    namespace cn = cnetmod;
    auto config = amqp10_example::configuration::from_environment();
    logger::info(
        "AMQP 1.0 service starting: broker={}:{} senders={} receivers={} messages={}",
        config.host, config.port, config.sender_concurrency,
        config.receiver_concurrency, config.message_count);
    cn::net_init network;
    auto context = cn::make_io_context();
    amqp10_example::application app(*context, std::move(config));
    cn::spawn(*context, app.start());
    context->run();
}
