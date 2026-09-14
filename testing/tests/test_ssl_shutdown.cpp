#include "test_framework.hpp"

import std;
import cnetmod.core.ssl;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

#include "ssl_shutdown_cases.inc"

RUN_TESTS()
