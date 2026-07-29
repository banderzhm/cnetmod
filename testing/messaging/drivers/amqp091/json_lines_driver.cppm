module;
#include <cnetmod/config.hpp>

export module cnetmod.testing.messaging.amqp091_driver;

import std;

export namespace cnetmod::testing::messaging::amqp091_driver {

/// Reads exactly one request per input line and writes one contract response.
auto run_json_lines(std::istream& input, std::ostream& output,
    std::ostream& diagnostics) -> int;

} // namespace cnetmod::testing::messaging::amqp091_driver
