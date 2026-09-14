enum class required_outage_scenario
{
    exhaust,
    recover,
    recover_then_exhaust
};

static void verify_required_outage_host(required_outage_scenario scenario)
{
    const bool restore_dependency = scenario != required_outage_scenario::exhaust;
    const bool second_outage = scenario == required_outage_scenario::recover_then_exhaust;
    const bool expect_automatic_stop = scenario != required_outage_scenario::recover;
    cnetmod::net_init network;
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    if (!reservation)
        return;
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"required-outage"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    service->recovery_options = {.initial_delay = std::chrono::milliseconds{5},
        .maximum_delay = std::chrono::milliseconds{10},
        .budget = std::chrono::milliseconds{200},
        .jitter = 0.0};
    auto built = application::application_builder{"required-outage"}
                     .configure([&](auto& value)
                         {
                             value.http.address = "127.0.0.1";
                             value.http.port = endpoint->port();
                             value.http.access_logging = false;
                             value.management.enabled = true;
                             value.management.same_port = true;
                             value.install_signal_handlers = false;
                             value.logging.manage_lifecycle = false;
                             value.observability.tracing = false;
                             value.observability.metrics = false;
                             value.observability.logs = false;
                             value.health.interval = std::chrono::milliseconds{5};
                             value.health.failures_before_down = 1;
                             value.health.successes_before_up = 1;
                         })
                     .service(service)
                     .build();
    ASSERT_TRUE(built.has_value());
    if (!built)
        return;
    reservation->close();
    std::optional<std::expected<void, std::error_code>> outcome;
    std::jthread runner{[&]
        {
            outcome = built->run();
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
        std::chrono::steady_clock::now() < startup_limit)
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    bool initially_ready = false;
    bool revoked = false;
    bool recovered = false;
    bool survived_old_budget = false;
    bool fresh_budget_observed = false;
    auto io = cnetmod::make_io_context();
    auto inspect = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::client client{*io, {.request_timeout = std::chrono::milliseconds{100}}};
        const auto url = std::format("http://127.0.0.1:{}", endpoint->port());
        const auto initial = co_await client.get(url + "/actuator/ready");
        initially_ready = initial && initial->status_code() == 200;
        service->dependency_available.store(false);
        for (unsigned attempt = 0; attempt < 20 && !revoked; ++attempt)
        {
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{5});
            const auto ready = co_await client.get(url + "/actuator/ready");
            revoked = ready && ready->status_code() == 503;
        }
        if (restore_dependency && revoked)
        {
            service->dependency_available.store(true);
            for (unsigned attempt = 0; attempt < 20 && !recovered; ++attempt)
            {
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{5});
                const auto ready = co_await client.get(url + "/actuator/ready");
                recovered = ready && ready->status_code() == 200;
            }
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{300});
            const auto ready = co_await client.get(url + "/actuator/ready");
            survived_old_budget = ready && ready->status_code() == 200 &&
                built->state() == application::application_state::running;
            if (second_outage && survived_old_budget)
            {
                service->dependency_available.store(false);
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{100});
                const auto second_ready = co_await client.get(url + "/actuator/ready");
                const auto live = co_await client.get(url + "/actuator/live");
                fresh_budget_observed = second_ready && second_ready->status_code() == 503 &&
                    live && live->status_code() == 200 &&
                    built->state() == application::application_state::running;
            }
        }
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
    const auto exit_limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (expect_automatic_stop && built->state() != application::application_state::stopped &&
        built->state() != application::application_state::cleanup_failed &&
        std::chrono::steady_clock::now() < exit_limit)
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    const bool automatic_stop = built->state() == application::application_state::stopped;
    built->request_stop();
    runner.join();
    ASSERT_TRUE(initially_ready);
    ASSERT_TRUE(revoked);
    ASSERT_EQ(automatic_stop, expect_automatic_stop);
    if (restore_dependency)
    {
        ASSERT_TRUE(recovered);
        ASSERT_TRUE(survived_old_budget);
    }
    if (second_outage)
        ASSERT_TRUE(fresh_budget_observed);
    ASSERT_TRUE(outcome.has_value());
    if (outcome)
        ASSERT_EQ(outcome->has_value(), !expect_automatic_stop);
    ASSERT_TRUE(built->state() == application::application_state::stopped);
    ASSERT_FALSE(built->health().ready());
    ASSERT_EQ(std::ranges::count(*events, "stop:" + service->key().canonical_name()), 1);
}

TEST(application_required_outage_revokes_http_readiness_and_stops_host)
{
    verify_required_outage_host(required_outage_scenario::exhaust);
}

TEST(application_required_recovery_restores_http_readiness_without_stale_shutdown)
{
    verify_required_outage_host(required_outage_scenario::recover);
}

TEST(application_second_required_outage_receives_fresh_bounded_recovery_budget)
{
    verify_required_outage_host(required_outage_scenario::recover_then_exhaust);
}
