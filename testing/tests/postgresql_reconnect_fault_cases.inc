enum class postgresql_reconnect_failure
{
    dispatch,
    frame,
    transport
};

static void verify_postgresql_reconnect_failure(postgresql_reconnect_failure failure)
{
    const bool before_dispatch = failure == postgresql_reconnect_failure::dispatch;
    const bool transport_failure = failure == postgresql_reconnect_failure::transport;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    cnetmod::postgresql::connection_pool_options options;
    options.minimum_connections = 0;
    options.maximum_connections = 1;
    options.connection.host = "127.0.0.1";
    options.connection.port = listener->local_endpoint()->port();
    options.connection.tls = cnetmod::postgresql::tls_mode::disable;
    options.connection.maximum_connect_attempts = 1;
    cnetmod::application::postgresql_service service{*io, options, "fault",
        cnetmod::application::service_requirement::required, {}};
    auto& pool = service.pool();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        const unsigned generations = transport_failure ? 3 : 2;
        for (unsigned generation = 0; generation != generations; ++generation)
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection.has_value());
            std::array<std::byte, 4096> bytes{};
            ASSERT_TRUE((co_await cnetmod::async_read(*io, *connection,
                             cnetmod::mutable_buffer{bytes.data(), bytes.size()}))
                    .has_value());
            if (transport_failure && generation == 1)
                continue;
            const std::array<std::uint8_t, 15> ready{
                'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};
            ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection,
                             cnetmod::const_buffer{ready.data(), ready.size()}))
                    .has_value());
            if (generation + 1 == generations)
                for (;;)
                {
                    auto received = co_await cnetmod::async_read(*io, *connection,
                        cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                    if (!received || *received == 0)
                        break;
                }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token initial;
        cnetmod::application::service_context context{*io, telemetry, supervisor, initial, {}};
        ASSERT_TRUE((co_await service.start(context)).has_value());
        auto first = co_await pool.acquire(initial);
        ASSERT_TRUE(first.has_value());
        cnetmod::cancel_token token;
        auto waiting = pool.acquire(token);
        waiting.handle().resume();
        ASSERT_EQ(pool.waiter_count(), std::size_t{1});
        if (before_dispatch)
            fail_after = 0;
        first->discard();
        if (before_dispatch)
            fail_after.reset();
        struct fail_queued_allocation
        {
            cnetmod::io_context& io;
            auto await_ready() const noexcept -> bool
            {
                return false;
            }
            void await_suspend(std::coroutine_handle<> handle)
            {
                io.post(handle);
                fail_after = 0;
            }
            void await_resume() noexcept
            {
                fail_after.reset();
            }
        };
        if (failure == postgresql_reconnect_failure::frame)
            co_await fail_queued_allocation{*io};
        if (transport_failure)
            while (!pool.background_error())
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(static_cast<bool>(pool.background_error()));
        if (before_dispatch)
            ASSERT_EQ(pool.background_error(), std::make_error_code(std::errc::not_enough_memory));
        const auto health = co_await service.probe(context);
        ASSERT_TRUE(health.status == cnetmod::application::service_health::down);
        ASSERT_EQ(health.error, pool.background_error());
        ASSERT_FALSE(waiting.handle().done());
        ASSERT_TRUE((co_await service.start(context)).has_value());
        while (!waiting.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        auto recovered = waiting.handle().promise().result();
        ASSERT_TRUE(recovered.has_value());
        ASSERT_FALSE(static_cast<bool>(pool.background_error()));
        ASSERT_EQ(pool.waiter_count(), std::size_t{0});
        *recovered = cnetmod::postgresql::pooled_connection{};
        ASSERT_TRUE((co_await service.stop(context)).has_value());
        ASSERT_EQ(pool.size(), std::size_t{0});
        finish();
    };
    auto peer_task = peer();
    auto exercise_task = exercise();
    io->post(peer_task.handle());
    io->post(exercise_task.handle());
    io->run();
    ASSERT_TRUE(peer_task.handle().done());
    ASSERT_TRUE(exercise_task.handle().done());
    peer_task.handle().promise().result();
    exercise_task.handle().promise().result();
}

TEST(postgresql_reconnect_frame_failure_is_reported_and_warmup_recovers)
{
    verify_postgresql_reconnect_failure(postgresql_reconnect_failure::frame);
}

TEST(postgresql_reconnect_dispatch_failure_is_reported_and_warmup_recovers)
{
    verify_postgresql_reconnect_failure(postgresql_reconnect_failure::dispatch);
}

TEST(postgresql_reconnect_transport_failure_is_reported_and_warmup_recovers)
{
    verify_postgresql_reconnect_failure(postgresql_reconnect_failure::transport);
}
