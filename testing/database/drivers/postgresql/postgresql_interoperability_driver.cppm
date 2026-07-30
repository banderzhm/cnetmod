module;

#include <cnetmod/config.hpp>

export module cnetmod.testing.database.postgresql_interoperability_driver;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;

export namespace cnetmod::testing::database {

auto execute_postgresql_interoperability_request(io_context& context,
    std::string request_json) -> task<std::string>;

} // namespace cnetmod::testing::database
