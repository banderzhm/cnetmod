#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
TEST(mysql_optional_outage_keeps_host_live_but_not_ready)
{
    cnetmod::net_init network;
    auto database_port = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    auto http_port = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(database_port.has_value() && http_port.has_value());
    if (!database_port || !http_port)
        return;
    ASSERT_TRUE(database_port->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(http_port->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto database_endpoint = database_port->local_endpoint();
    const auto http_endpoint = http_port->local_endpoint();
    ASSERT_TRUE(database_endpoint.has_value() && http_endpoint.has_value());
    if (!database_endpoint || !http_endpoint)
        return;
    auto events = std::make_shared<std::vector<std::string>>();
    auto required = std::make_shared<fake_service>(application::service_key{"required-sibling"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto built = application::application_builder{"mysql-optional-outage"}
                     .configure([&](application::application_configuration& value)
                         {
                             value.http.address = "127.0.0.1";
                             value.http.port = http_endpoint->port();
                             value.http.access_logging = false;
                             value.management.enabled = true;
                             value.management.same_port = true;
                             value.install_signal_handlers = false;
                             value.logging.manage_lifecycle = false;
                             value.observability.tracing = false;
                             value.observability.metrics = false;
                             value.observability.logs = false;
                             value.health.interval = std::chrono::milliseconds{10};
                             value.health.timeout = std::chrono::milliseconds{10};
                             value.lifecycle.service_start_timeout = std::chrono::milliseconds{20};
                             application::configured_service database{.name = "mysql", .enabled = true, .requirement = application::service_requirement::optional};
                             database.recovery = {.initial_delay = std::chrono::milliseconds{1},
                                 .maximum_delay = std::chrono::milliseconds{1},
                                 .budget = std::chrono::milliseconds{60},
                                 .jitter = 0.0};
                             database.properties = cnetmod::json::object();
                             database.properties["host"] = "127.0.0.1";
                             database.properties["port"] = database_endpoint->port();
                             database.properties["username"] = "test-only";
                             database.properties["database"] = "test-only";
                             database.properties["minimum_size"] = 1;
                             database.properties["maximum_size"] = 1;
                             value.services.emplace("database", std::move(database));
                         })
                     .enable_auto_configuration()
                     .service(required)
                     .build();
    ASSERT_TRUE(built.has_value());
    if (!built)
        return;
    http_port->close();
    std::optional<std::expected<void, std::error_code>> result;
    std::jthread runner{[&]
        {
            result = built->run();
        }};

    struct stop_on_exit
    {
        application::application_host& host;

        ~stop_on_exit()
        {
            host.request_stop();
        }
    } stop{*built};

    const auto startup_limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (built->state() != application::application_state::running &&
        built->state() != application::application_state::stopped &&
        std::chrono::steady_clock::now() < startup_limit)
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    bool verified = false;
    auto io = cnetmod::make_io_context();
    auto inspect = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::client client{*io, {.request_timeout = std::chrono::milliseconds{300}}};
        const auto url = std::format("http://127.0.0.1:{}", http_endpoint->port());
        for (unsigned check = 0; check < 3; ++check)
        {
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{80});
            auto live = co_await client.get(url + "/actuator/live");
            auto ready = co_await client.get(url + "/actuator/ready");
            ASSERT_TRUE(live && live->status_code() == 200);
            ASSERT_TRUE(ready && ready->status_code() == 503);
            ASSERT_TRUE(built->state() == application::application_state::running);
        }
        verified = true;
        io->stop();
    };
    if (built->state() == application::application_state::running)
    {
        auto operation = inspect();
        io->post(operation.handle());
        io->run();
        ASSERT_TRUE(operation.handle().done());
        operation.handle().promise().result();
    }
    built->request_stop();
    runner.join();
    ASSERT_TRUE(verified);
    ASSERT_TRUE(result.has_value());
    if (result)
        ASSERT_TRUE(result->has_value());
    ASSERT_TRUE(built->state() == application::application_state::stopped);
    ASSERT_FALSE(built->health().ready());
    auto registered = built->services().require<application::mysql_service>("default");
    ASSERT_TRUE(registered.has_value());
    ASSERT_EQ(registered->get().pool().waiter_count(), 0U);
    ASSERT_EQ(events->size(), 2U);
    if (events->size() == 2)
    {
        ASSERT_TRUE(events->front().starts_with("start:"));
        ASSERT_TRUE(events->back().starts_with("stop:"));
    }
}

TEST(mysql_host_failed_start_cleans_supervised_pool)
{
    for (const bool optional : {false, true})
    {
        cnetmod::net_init network;
        auto reserved = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(reserved.has_value());
        ASSERT_TRUE(reserved->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        auto endpoint = reserved->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        auto built = application::application_builder{"mysql-start-failure"}
                         .configure([&](application::application_configuration& value)
                             {
                                 value.management.enabled = false;
                                 value.install_signal_handlers = false;
                                 value.logging.manage_lifecycle = false;
                                 value.observability.tracing = false;
                                 value.observability.metrics = false;
                                 value.observability.logs = false;
                                 value.lifecycle.service_start_timeout = std::chrono::milliseconds{optional ? 200 : 30};
                                 value.lifecycle.total_start_timeout = std::chrono::milliseconds{optional ? 30 : 100};
                                 value.lifecycle.total_stop_timeout = std::chrono::milliseconds{200};
                                 application::configured_service database{.name = "mysql", .enabled = true};
                                 database.requirement = optional ? application::service_requirement::optional
                                                                 : application::service_requirement::required;
                                 database.properties = cnetmod::json::object();
                                 database.properties["host"] = "127.0.0.1";
                                 database.properties["port"] = endpoint->port();
                                 database.properties["username"] = "test-only";
                                 database.properties["database"] = "test-only";
                                 database.properties["minimum_size"] = 1;
                                 database.properties["maximum_size"] = 1;
                                 value.services.emplace("database", std::move(database));
                             })
                         .enable_auto_configuration()
                         .build();
        ASSERT_TRUE(built.has_value());
        if (!built)
            return;
        const auto began = std::chrono::steady_clock::now();
        const auto result = built->run();
        ASSERT_FALSE(result.has_value());
        if (!result)
            ASSERT_TRUE(result.error() == std::errc::timed_out);
        ASSERT_TRUE(std::chrono::steady_clock::now() - began < std::chrono::seconds{2});
        ASSERT_TRUE(built->state() == application::application_state::stopped);
        ASSERT_FALSE(built->health().ready());
        auto registered = built->services().require<application::mysql_service>("default");
        ASSERT_TRUE(registered.has_value());
        auto& service = registered->get();
        ASSERT_EQ(service.pool().waiter_count(), 0U);
    }
}

TEST(mysql_required_recovery_budget_stops_pool_maintenance)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto reserved = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(reserved.has_value());
    ASSERT_TRUE(reserved->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    auto endpoint = reserved->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    // Keep the port reserved without listening: no external service is changed.
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    cnetmod::mysql::pool_params options;
    options.port = endpoint->port();
    options.ssl = cnetmod::mysql::ssl_mode::disable;
    options.initial_size = options.max_size = 1;
    auto service = std::make_shared<application::mysql_service>(*io, options,
        "unavailable", application::service_requirement::required,
        application::recovery_policy{.initial_delay = std::chrono::milliseconds{1},
            .maximum_delay = std::chrono::milliseconds{1},
            .budget = std::chrono::milliseconds{60},
            .jitter = 0.0});
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health,
        {.service_start_timeout = std::chrono::milliseconds{10}}};
    unsigned exhausted = 0;
    supervisor.on_recovery_exhausted([&](std::string_view, std::error_code error)
        {
            ASSERT_TRUE(error == std::errc::timed_out);
            ++exhausted;
            supervisor.request_stop();
        });
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto began = std::chrono::steady_clock::now();
        ASSERT_TRUE(lifecycle.schedule_recovery(service->key()));
        auto joined = co_await supervisor.join();
        ASSERT_TRUE(std::chrono::steady_clock::now() - began >= std::chrono::milliseconds{60});
        ASSERT_FALSE(joined.has_value());
        ASSERT_EQ(exhausted, 1U);
        ASSERT_EQ(service->pool().waiter_count(), 0U);
        ASSERT_EQ(lifecycle.started_services().size(), 1U);
        ASSERT_TRUE(service->shutdown_required());
        auto failure = lifecycle.last_failure();
        ASSERT_TRUE(failure.has_value());
        if (failure)
        {
            ASSERT_TRUE(failure->phase == application::lifecycle_phase::health_recovery);
            ASSERT_TRUE(failure->error == std::errc::timed_out);
        }
        const auto stopped = co_await lifecycle.stop();
        ASSERT_FALSE(stopped.has_value());
        ASSERT_EQ(stopped.error(), joined.error());
        ASSERT_TRUE(lifecycle.started_services().empty());
        ASSERT_FALSE(service->shutdown_required());
        auto acquired = co_await service->pool().async_get_connection(
            cnetmod::deadline::after(std::chrono::milliseconds{10}));
        ASSERT_FALSE(acquired.has_value());
        if (!acquired)
            ASSERT_TRUE(acquired.error() == std::errc::operation_canceled);
        completed = true;
        io->stop();
    };
    auto operation = run();
    io->post(operation.handle());
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
    ASSERT_TRUE(completed);
}
#endif
