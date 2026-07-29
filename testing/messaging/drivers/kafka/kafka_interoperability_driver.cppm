module;

#include <cnetmod/config.hpp>

export module cnetmod.testing.messaging.kafka_interoperability_driver;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;

export namespace cnetmod::testing::messaging {

// Executes one version-1 JSON-lines Kafka interoperability request.  The
// returned string is the complete response envelope and never contains a
// trailing newline.
auto execute_kafka_interoperability_request(io_context& context,
    std::string request_json)
    -> task<std::string>;

} // namespace cnetmod::testing::messaging
