TEST(postgresql_partial_startup_timeout_cleans_connections_under_lifecycle)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::postgresql::connection_pool_options options;
    options.connection.host = "127.0.0.1";
    options.connection.port = endpoint->port();
    options.connection.tls = cnetmod::postgresql::tls_mode::disable;
    options.connection.maximum_connect_attempts = 1;
    options.minimum_connections = 2;
    options.maximum_connections = 2;
    auto service_owner = std::make_shared<cnetmod::application::postgresql_service>(*io, options, "partial",
        cnetmod::application::service_requirement::required, cnetmod::application::recovery_policy{});
    auto& service = *service_owner;
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::application::service_registry services;
    cnetmod::application::health_registry health;
    ASSERT_TRUE(services.manage(service_owner));
    ASSERT_TRUE(health.add(service_owner));
    services.freeze();
    cnetmod::application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health,
        {.service_start_timeout = std::chrono::milliseconds{100}}};
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto first = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(first.has_value());
        std::array<std::byte, 4096> bytes{};
        ASSERT_TRUE((co_await cnetmod::async_read(*io, *first,
                         cnetmod::mutable_buffer{bytes.data(), bytes.size()}))
                .has_value());
        const std::array<std::uint8_t, 15> ready{
            'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *first,
                         cnetmod::const_buffer{ready.data(), ready.size()}))
                .has_value());
        auto second = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE((co_await cnetmod::async_read(*io, *second,
                         cnetmod::mutable_buffer{bytes.data(), bytes.size()}))
                .has_value());
        auto cancelled = co_await cnetmod::async_read(*io, *second,
            cnetmod::mutable_buffer{bytes.data(), bytes.size()});
        ASSERT_TRUE(!cancelled || *cancelled == 0);
        for (;;)
        {
            auto received = co_await cnetmod::async_read(*io, *first,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            if (!received || *received == 0)
                break;
        }
        std::vector<cnetmod::socket> recovered;
        for (unsigned index = 0; index != 2; ++index)
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection.has_value());
            ASSERT_TRUE((co_await cnetmod::async_read(*io, *connection,
                             cnetmod::mutable_buffer{bytes.data(), bytes.size()}))
                    .has_value());
            ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection,
                             cnetmod::const_buffer{ready.data(), ready.size()}))
                    .has_value());
            recovered.push_back(std::move(*connection));
        }
        for (auto& connection : recovered)
        {
            for (;;)
            {
                auto received = co_await cnetmod::async_read(*io, connection,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                if (!received || *received == 0)
                    break;
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token token;
        cnetmod::application::service_context context{*io, telemetry, supervisor, token,
            cnetmod::deadline::after(std::chrono::milliseconds{100})};
        auto started = co_await lifecycle.start();
        ASSERT_FALSE(started.has_value());
        ASSERT_EQ(started.error(), std::make_error_code(std::errc::timed_out));
        ASSERT_EQ(service.pool().checked_out_count(), std::size_t{0});
        ASSERT_EQ(service.pool().idle_count(), std::size_t{0});
        ASSERT_EQ(service.pool().size(), std::size_t{2});
        ASSERT_EQ(lifecycle.active_service_count(), std::size_t{0});
        ASSERT_FALSE(service.cleanup_required());
        ASSERT_FALSE(service.shutdown_required());
        const auto failure = lifecycle.last_failure();
        ASSERT_TRUE(failure.has_value());
        ASSERT_TRUE(failure->service == service.key());
        ASSERT_TRUE(failure->phase == cnetmod::application::lifecycle_phase::startup);
        ASSERT_EQ(failure->error, started.error());
        auto health = co_await service.probe(context);
        ASSERT_TRUE(health.status == cnetmod::application::service_health::down);
        ASSERT_TRUE((co_await lifecycle.start()).has_value());
        ASSERT_EQ(service.pool().idle_count(), std::size_t{2});
        ASSERT_EQ(lifecycle.active_service_count(), std::size_t{1});
        ASSERT_TRUE((co_await lifecycle.stop()).has_value());
        ASSERT_EQ(service.pool().size(), std::size_t{0});
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
