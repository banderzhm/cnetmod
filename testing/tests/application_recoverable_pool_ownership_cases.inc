namespace {

template <typename Service, typename Options>
void verify_recoverable_pool_ownership(Options options)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto reserved = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(reserved.has_value());
    if (!reserved)
        return;
    ASSERT_TRUE(reserved->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    auto endpoint = reserved->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    options.host = "127.0.0.1";
    options.port = endpoint->port();
    options.initial_size = 1;
    options.max_size = 1;
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    auto service = std::make_shared<Service>(*io, options, "unavailable",
        application::service_requirement::optional, application::recovery_policy{});
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::lifecycle_policy policy;
    policy.service_start_timeout = std::chrono::milliseconds{20};
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health, policy};
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        ASSERT_TRUE(service->shutdown_required());
        ASSERT_FALSE(service->cleanup_required());
        const auto owned = lifecycle.started_services();
        ASSERT_EQ(owned.size(), 1U);
        if (!owned.empty())
            ASSERT_TRUE(owned.front() == service->key());
        supervisor.request_stop();
        (void)co_await supervisor.join();
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_FALSE(service->shutdown_required());
        ASSERT_TRUE(lifecycle.started_services().empty());
        ASSERT_EQ(service->pool().checked_out_count(), 0U);
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    operation.handle().promise().result();
}

} // namespace

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
TEST(application_mysql_failed_admission_retains_shutdown_ownership)
{
    cnetmod::mysql::pool_params options;
    options.ssl = cnetmod::mysql::ssl_mode::disable;
    verify_recoverable_pool_ownership<application::mysql_service>(options);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
TEST(application_redis_failed_admission_retains_shutdown_ownership)
{
    verify_recoverable_pool_ownership<application::redis_service>(cnetmod::redis::pool_params{});
}
#endif
