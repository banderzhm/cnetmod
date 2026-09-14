TEST(application_partial_start_cleanup_retains_dependency_and_original_error)
{
    for (bool throws : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::service_registry services;
        application::health_registry health;
        application::task_supervisor supervisor{*io};
        auto events = std::make_shared<std::vector<std::string>>();
        auto dependency = std::make_shared<fake_service>(application::service_key{"dependency"},
            std::vector<application::service_key>{}, application::service_requirement::required, events);
        auto partial = std::make_shared<fake_service>(application::service_key{"partial"},
            std::vector<application::service_key>{dependency->key()}, application::service_requirement::required, events, 1U);
        partial->retain_failed_start = true;
        partial->stop_failures_remaining = 1;
        const auto original = std::make_error_code(std::errc::connection_refused);
        if (throws)
            partial->start_exception = original;
        for (const auto& service : {dependency, partial})
        {
            ASSERT_TRUE(services.manage(service));
            ASSERT_TRUE(health.add(service));
        }
        services.freeze();
        application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
        auto run = [&]() -> cnetmod::task<void>
        {
            const auto result = co_await lifecycle.start();
            ASSERT_FALSE(result.has_value());
            ASSERT_EQ(result.error(), original);
            ASSERT_EQ(lifecycle.started_services().size(), 2U);
            ASSERT_TRUE(partial->cleanup_required());
            ASSERT_TRUE(lifecycle.last_failure().has_value());
            ASSERT_EQ(lifecycle.last_failure()->error, original);
            ASSERT_TRUE(lifecycle.last_rollback_failure().has_value());
            ASSERT_TRUE(lifecycle.last_rollback_failure()->service == partial->key());
            ASSERT_TRUE(co_await lifecycle.stop());
            ASSERT_FALSE(partial->cleanup_required());
            ASSERT_TRUE(lifecycle.started_services().empty());
            ASSERT_TRUE(co_await lifecycle.stop());
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        operation.handle().promise().result();
        ASSERT_TRUE(*events == std::vector<std::string>({"start:dependency:default", "start:partial:default", "stop:partial:default", "stop:partial:default", "stop:dependency:default"}));
    }
}

TEST(application_optional_partial_start_cleans_before_recovery)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"optional"},
        std::vector<application::service_key>{}, application::service_requirement::optional, events, 2U);
    service->retain_failed_start = true;
    service->recovery_options.initial_delay = std::chrono::milliseconds{1};
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        (void)co_await supervisor.join();
        ASSERT_TRUE(service->has_started);
        ASSERT_FALSE(service->cleanup_required());
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_TRUE(lifecycle.started_services().empty());
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    operation.handle().promise().result();
    ASSERT_TRUE(*events == std::vector<std::string>({"start:optional:default", "stop:optional:default", "start:optional:default", "stop:optional:default", "start:optional:default", "stop:optional:default"}));
}
