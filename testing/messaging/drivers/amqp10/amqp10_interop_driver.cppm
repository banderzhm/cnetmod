module;

#include <cnetmod/config.hpp>

export module cnetmod.testing.messaging.amqp10_interop_driver;

import std;

export namespace cnetmod::testing::messaging::amqp10 {

/// Serves version 1 of the messaging test JSON-lines contract.
/// Exactly one response line is written for every non-empty request line.
auto run_json_lines(std::istream& input, std::ostream& output,
    std::ostream& diagnostics) -> int;

} // namespace cnetmod::testing::messaging::amqp10
