#include "test_framework.hpp"

import std;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.protocol.mysql;

#include "mysql_pool_reset_cases.inc"

RUN_TESTS()
