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
import cnetmod.coro.semaphore;
import cnetmod.coro.timer;

#include "ssl_shutdown_cases.inc"

TEST(ssl_default_ca_loads_platform_trust_store)
{
    auto context = cnetmod::ssl_context::client();
    ASSERT_TRUE(context.has_value());
    if (!context)
        return;

    auto loaded = context->set_default_ca();
    ASSERT_TRUE(loaded.has_value());
}

RUN_TESTS()
