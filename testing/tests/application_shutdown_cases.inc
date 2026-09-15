static void verify_application_bind_failure_cleanup(bool transient_stop_failure)
{
    cnetmod::net_init network;
    auto occupied = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(occupied.has_value());
    if (!occupied)
        return;
    ASSERT_TRUE(occupied->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(occupied->listen().has_value());
    const auto endpoint = occupied->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    auto io = cnetmod::make_io_context();
    cnetmod::http::server probe{*io};
    const auto original = probe.listen("127.0.0.1", endpoint->port());
    ASSERT_FALSE(original.has_value());
    if (original)
        return;
    auto events = std::make_shared<std::vector<std::string>>();
    const application::service_key dependency_key{"custom", "bind-dependency"};
    auto dependency = std::make_shared<fake_service>(dependency_key,
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto service = std::make_shared<fake_service>(application::service_key{"custom", "bind-cleanup"},
        std::vector<application::service_key>{dependency_key}, application::service_requirement::required, events);
    if (transient_stop_failure)
        service->stop_failures_remaining = 2;
    else
        service->stop_delay = std::chrono::seconds{10};
    auto built = application::application_builder{"occupied-port"}
                     .configure([&](auto& value)
                         {
                             value.http.address = "127.0.0.1";
                             value.http.port = endpoint->port();
                             value.management.enabled = false;
                             value.logging.manage_lifecycle = false;
                             value.install_signal_handlers = false;
                             value.lifecycle.total_stop_timeout = std::chrono::milliseconds{transient_stop_failure ? 150 : 50};
                             value.lifecycle.service_stop_timeout = std::chrono::milliseconds{200};
                         })
                     .service(service)
                     .service(dependency)
                     .build();
    ASSERT_TRUE(built.has_value());
    if (!built)
        return;
    const auto started = std::chrono::steady_clock::now();
    const auto result = built->run();
    ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
    ASSERT_FALSE(result.has_value());
    if (!result)
        ASSERT_EQ(result.error(), original.error());
    if (transient_stop_failure)
    {
        ASSERT_TRUE(built->state() == application::application_state::stopped);
        ASSERT_EQ(std::count(events->begin(), events->end(), "stop:custom:bind-cleanup"), 3);
        ASSERT_EQ(std::count(events->begin(), events->end(), "stop:custom:bind-dependency"), 1);
        ASSERT_EQ(events->back(), std::string{"stop:custom:bind-dependency"});
    }
    else
    {
        ASSERT_TRUE(service->stop_cancelled);
        ASSERT_TRUE(service->stop_settled);
        ASSERT_TRUE(built->state() == application::application_state::cleanup_failed);
        ASSERT_EQ(std::count(events->begin(), events->end(), "stop:custom:bind-dependency"), 0);
        for (const auto budget : {std::chrono::milliseconds{-1}, std::chrono::milliseconds{0},
                 std::chrono::milliseconds::max()})
        {
            const auto rejected = built->retry_cleanup(budget);
            ASSERT_FALSE(rejected.has_value());
            if (!rejected)
                ASSERT_TRUE(rejected.error() == std::errc::invalid_argument);
            ASSERT_TRUE(built->state() == application::application_state::cleanup_failed);
        }
        service->stop_delay = std::chrono::milliseconds::zero();
        ASSERT_TRUE(built->retry_cleanup(std::chrono::milliseconds{200}).has_value());
        ASSERT_TRUE(built->state() == application::application_state::stopped);
        ASSERT_EQ(std::count(events->begin(), events->end(), "stop:custom:bind-dependency"), 1);
        const auto event_count = events->size();
        ASSERT_TRUE(built->retry_cleanup(std::chrono::milliseconds{200}).has_value());
        ASSERT_EQ(events->size(), event_count);
        ASSERT_FALSE(built->run().has_value());
    }
}

TEST(application_bind_failure_preserves_error_with_slow_cleanup)
{
    verify_application_bind_failure_cleanup(false);
}

TEST(application_bind_failure_retries_cleanup_before_closing_dependencies)
{
    verify_application_bind_failure_cleanup(true);
}

TEST(application_task_registration_failure_settles_listeners_and_services)
{
    cnetmod::net_init network;
    for (const auto* name : {"application-management-http", "application-business-http", "application-health"})
    {
        auto business = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        auto management = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(business.has_value() && management.has_value());
        ASSERT_TRUE(business->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(management->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        const auto business_endpoint = business->local_endpoint();
        const auto management_endpoint = management->local_endpoint();
        ASSERT_TRUE(business_endpoint.has_value() && management_endpoint.has_value());
        auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(application::service_key{"custom", "collision"},
            std::vector<application::service_key>{}, application::service_requirement::required, events);
        service->reserved_task_name = name;
        auto built = application::application_builder{"task-registration-failure"}
                         .configure([&](auto& value)
                             {
                                 value.http.address = "127.0.0.1";
                                 value.http.port = business_endpoint->port();
                                 value.management.enabled = true;
                                 value.management.same_port = false;
                                 value.management.address = "127.0.0.1";
                                 value.management.port = management_endpoint->port();
                                 value.logging.manage_lifecycle = false;
                                 value.install_signal_handlers = false;
                                 value.observability.tracing = false;
                                 value.observability.metrics = false;
                                 value.observability.logs = false;
                             })
                         .service(service)
                         .build();
        ASSERT_TRUE(built.has_value());
        business->close();
        management->close();
        const auto result = built->run();
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), std::make_error_code(std::errc::file_exists));
        ASSERT_TRUE(built->state() == application::application_state::stopped);
        ASSERT_FALSE(built->health().ready());
        ASSERT_TRUE(service->reserved_task_settled);
        ASSERT_EQ(std::count(events->begin(), events->end(), "stop:custom:collision"), 1);
    }
}

TEST(application_health_loop_tracks_dependency_loss_and_recovery)
{
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
    auto service = std::make_shared<fake_service>(application::service_key{"custom", "primary"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto built = application::application_builder{"health-runtime"}
                     .configure([&](auto& value)
                         {
                             value.http.address = "127.0.0.1";
                             value.http.port = endpoint->port();
                             value.management.enabled = false;
                             value.logging.manage_lifecycle = false;
                             value.install_signal_handlers = false;
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
    bool initially_ready{};
    bool observed_down{};
    bool recovered{};
    std::jthread control([&]
        {
            auto wait_for = [&](auto condition)
            {
                const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                while (!condition() && std::chrono::steady_clock::now() < limit)
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                return condition();
            };
            initially_ready = wait_for([&]
                {
                    return built->health().ready();
                });
            service->dependency_available.store(false);
            observed_down = wait_for([&]
                {
                    const auto snapshots = built->health().snapshots();
                    return !built->health().ready() && snapshots.size() == 1 &&
                        snapshots.front().report.status == application::service_health::down;
                });
            service->dependency_available.store(true);
            recovered = wait_for([&]
                {
                    return built->health().ready();
                });
            built->request_stop();
        });
    const auto result = built->run();
    control.join();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(initially_ready && observed_down && recovered);
    ASSERT_TRUE(service->probes >= 3);
    ASSERT_FALSE(built->health().ready());
}

TEST(application_shutdown_aborts_incomplete_http_body_before_destruction)
{
    cnetmod::net_init network;
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    std::atomic<bool> entered{};
    std::atomic<bool> incomplete_handler_entered{};
    auto events = std::make_shared<std::vector<std::string>>();
    auto slow_service = std::make_shared<fake_service>(application::service_key{"custom", "slow-close"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    slow_service->stop_delay = std::chrono::seconds{10};
    constexpr auto total_stop_timeout = std::chrono::milliseconds{50};
    auto built = application::application_builder{"incomplete-http-body"}
                     .service(slow_service)
                     .configure([&](auto& value)
                         {
                             value.http.address = "127.0.0.1";
                             value.http.port = endpoint->port();
                             value.http.access_logging = false;
                             value.management.enabled = false;
                             value.logging.manage_lifecycle = false;
                             value.install_signal_handlers = false;
                             value.lifecycle.http_drain_timeout = std::chrono::milliseconds{200};
                             value.lifecycle.total_stop_timeout = total_stop_timeout;
                             value.lifecycle.service_stop_timeout = std::chrono::milliseconds{200};
                         })
                     .routes([&](cnetmod::http::router& routes)
                         {
                             routes.get("/ready", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
                                 {
                                     entered.store(true);
                                     request.text(cnetmod::http::status::ok, "ready");
                                     co_return;
                                 });
                             routes.post("/incomplete", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
                                 {
                                     incomplete_handler_entered.store(true);
                                     (void)co_await request.read_full_body();
                                 });
                         })
                     .build();
    ASSERT_TRUE(built.has_value());
    reservation->close();
    std::optional<std::expected<void, std::error_code>> result;
    std::jthread runner([&]
        {
            result = built->run();
        });
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (built->state() != application::application_state::running &&
        std::chrono::steady_clock::now() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    auto client_io = cnetmod::make_io_context();
    bool closed{};
    std::chrono::steady_clock::time_point stop_requested_at;
    auto exchange = [&]() -> cnetmod::task<void>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*client_io, *peer, *endpoint)).has_value());
        const std::string_view initial = "GET /ready HTTP/1.1\r\nHost: localhost\r\n\r\n";
        ASSERT_TRUE((co_await cnetmod::async_write_all(*client_io, *peer,
                         cnetmod::const_buffer{initial.data(), initial.size()}))
                .has_value());
        char buffer[1024];
        std::string response;
        while (!response.ends_with("ready"))
        {
            cnetmod::cancel_token token;
            const auto read = co_await cnetmod::with_timeout(*client_io, std::chrono::seconds{2},
                cnetmod::async_read(*client_io, *peer, cnetmod::mutable_buffer{buffer, sizeof(buffer)}, token), token);
            if (!read || *read == 0)
                break;
            response.append(buffer, *read);
        }
        ASSERT_TRUE(response.ends_with("ready"));
        const std::string_view header = "POST /incomplete HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\n";
        ASSERT_TRUE((co_await cnetmod::async_write_all(*client_io, *peer,
                         cnetmod::const_buffer{header.data(), header.size()}))
                .has_value());
        stop_requested_at = std::chrono::steady_clock::now();
        built->request_stop();
        cnetmod::cancel_token token;
        const auto read = co_await cnetmod::with_timeout(*client_io, std::chrono::seconds{2},
            cnetmod::async_read(*client_io, *peer, cnetmod::mutable_buffer{buffer, sizeof(buffer)}, token), token);
        closed = (read && *read == 0) ||
            (!read && read.error() == cnetmod::make_error_code(cnetmod::errc::end_of_file));
        client_io->stop();
    };
    cnetmod::spawn(*client_io, exchange());
    client_io->run();
    runner.join();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_requested_at;
    ASSERT_TRUE(stop_elapsed < total_stop_timeout + std::chrono::milliseconds{500});
    ASSERT_TRUE(slow_service->stop_cancelled);
    ASSERT_TRUE(slow_service->stop_settled);
    ASSERT_TRUE(built->state() == application::application_state::cleanup_failed);
    ASSERT_TRUE(entered.load());
    ASSERT_FALSE(incomplete_handler_entered.load());
    ASSERT_TRUE(closed);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
    ASSERT_EQ(result->error(), std::make_error_code(std::errc::timed_out));
}

TEST(application_preserves_request_drain_timeout_after_handler_settles)
{
    cnetmod::net_init network;
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    application::application_host* host = nullptr;
    bool handler_settled = false;
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"handler-dependency"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto built = application::application_builder{"drain-timeout"}
                     .configure([&](auto& value)
                         {
                             value.http.address = "127.0.0.1";
                             value.http.port = endpoint->port();
                             value.http.access_logging = false;
                             value.management.enabled = false;
                             value.logging.manage_lifecycle = false;
                             value.install_signal_handlers = false;
                             value.observability.tracing = false;
                             value.observability.metrics = false;
                             value.observability.logs = false;
                             value.lifecycle.http_drain_timeout = std::chrono::milliseconds{200};
                             value.lifecycle.service_stop_timeout = std::chrono::seconds{1};
                         })
                     .routes([&](cnetmod::http::router& routes)
                         {
                             routes.get("/slow", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
                                 {
                                     host->request_stop();
                                     co_await cnetmod::async_sleep(request.io_ctx(), std::chrono::milliseconds{500});
                                     handler_settled = true;
                                     events->push_back("handler-settled");
                                     request.text(cnetmod::http::status::ok, "settled");
                                 });
                         })
                     .service(service)
                     .build();
    ASSERT_TRUE(built.has_value());
    host = &*built;
    reservation->close();
    std::optional<std::expected<void, std::error_code>> outcome;
    std::jthread runner([&]
        {
            outcome = host->run();
        });
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (host->state() != application::application_state::running && std::chrono::steady_clock::now() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    auto io = cnetmod::make_io_context();
    auto exchange = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::client client{*io};
        (void)co_await client.get(std::format("http://127.0.0.1:{}/slow", endpoint->port()));
        client.close();
        host->request_stop();
        io->stop();
    };
    auto request = exchange();
    request.handle().resume();
    io->run();
    runner.join();
    ASSERT_TRUE(handler_settled);
    const auto handler_end = std::find(events->begin(), events->end(), "handler-settled");
    const auto service_end = std::find(events->begin(), events->end(), "stop:handler-dependency:default");
    ASSERT_TRUE(handler_end != events->end() && service_end != events->end());
    ASSERT_TRUE(handler_end < service_end);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    ASSERT_EQ(outcome->error(), std::make_error_code(std::errc::timed_out));
    ASSERT_TRUE(host->state() == application::application_state::stopped);
}

TEST(application_build_registers_managed_service_health)
{
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"custom", "primary"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto built = application::application_builder{"health-registration"}
                     .configure([](auto& value)
                         {
                             value.logging.manage_lifecycle = false;
                         })
                     .service(service)
                     .build();
    ASSERT_TRUE(built.has_value());
    if (built)
        ASSERT_EQ(built->health().snapshots().size(), 1U);
    ASSERT_TRUE(events->empty());
}

TEST(application_startup_failure_settles_previously_accepted_telemetry)
{
    for (const bool service_failure : {false, true})
    {
        cnetmod::net_init network;
        auto occupied = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(occupied.has_value());
        if (!occupied)
            return;
        ASSERT_TRUE(occupied->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(occupied->listen().has_value());
        const auto endpoint = occupied->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        if (!endpoint)
            return;
        auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(application::service_key{"bind-cleanup"},
            std::vector<application::service_key>{}, application::service_requirement::required, events);
        auto dependent = std::make_shared<fake_service>(application::service_key{"dependent"},
            std::vector<application::service_key>{service->key()}, application::service_requirement::required, events);
        if (service_failure)
            dependent->start_error = std::make_error_code(std::errc::permission_denied);
        std::error_code binding_error;
        {
            auto probe_io = cnetmod::make_io_context();
            cnetmod::http::server probe{*probe_io};
            const auto listening = probe.listen("127.0.0.1", endpoint->port());
            ASSERT_FALSE(listening.has_value());
            binding_error = listening.error();
        }
        service->stop_delay = std::chrono::milliseconds{30};
        service->stop_failures_remaining = service_failure ? 1 : 0;
        auto built = application::application_builder{"startup-failure-telemetry"}
                         .configure([&](auto& value)
                             {
                                 value.http.address = "127.0.0.1";
                                 value.http.port = endpoint->port();
                                 value.management.enabled = false;
                                 value.install_signal_handlers = false;
                                 value.logging.manage_lifecycle = false;
                                 value.observability.tracing = false;
                                 value.observability.metrics = false;
                                 value.observability.logs = true;
                                 value.observability.otlp = {};
                                 value.observability.otlp.logs_endpoint = std::format("http://127.0.0.1:{}/v1/logs", endpoint->port());
                                 value.observability.otlp.request_timeout = std::chrono::seconds{5};
                                 value.lifecycle.telemetry_flush_timeout = std::chrono::seconds{5};
                                 value.lifecycle.total_stop_timeout = std::chrono::milliseconds{100};
                             })
                         .service(service)
                         .service(dependent)
                         .build();
        ASSERT_TRUE(built.has_value());
        if (!built)
            return;
        ASSERT_TRUE(built->telemetry().submit_log({.body = "before-start"}));
        const auto started = std::chrono::steady_clock::now();
        const auto result = built->run();
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), service_failure ? dependent->start_error : binding_error);
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
        ASSERT_TRUE(built->state() == application::application_state::stopped);
        ASSERT_TRUE(service->stop_settled);
        ASSERT_EQ(std::ranges::count(*events, "stop:" + service->key().canonical_name()), service_failure ? 2 : 1);
        ASSERT_EQ(std::ranges::count(*events, "stop:" + dependent->key().canonical_name()), service_failure ? 0 : 1);
        const auto statistics = built->telemetry().statistics();
        ASSERT_TRUE(statistics.failed_batches + statistics.dropped_logs > 0);
    }
}

TEST(application_shutdown_cancels_silent_collector_before_host_destruction)
{
    cnetmod::net_init network;
    auto collector_io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value() && reservation.has_value());
    if (!listener || !reservation)
        return;
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto remote = listener->local_endpoint();
    const auto business = reservation->local_endpoint();
    ASSERT_TRUE(remote.has_value() && business.has_value());
    if (!remote || !business)
        return;
    auto built = application::application_builder{"shutdown-silent-collector"}
                     .configure([&](auto& value)
                         {
                             value.http.address = "127.0.0.1";
                             value.http.port = business->port();
                             value.http.access_logging = false;
                             value.management.enabled = false;
                             value.install_signal_handlers = false;
                             value.logging.manage_lifecycle = false;
                             value.observability.tracing = false;
                             value.observability.metrics = false;
                             value.observability.logs = true;
                             value.observability.otlp = {};
                             value.observability.otlp.logs_endpoint = std::format("http://127.0.0.1:{}/v1/logs", remote->port());
                             value.observability.otlp.request_timeout = std::chrono::seconds{5};
                             value.lifecycle.telemetry_flush_timeout = std::chrono::seconds{5};
                             value.lifecycle.total_stop_timeout = std::chrono::milliseconds{100};
                             value.lifecycle.http_drain_timeout = std::chrono::milliseconds{10};
                         })
                     .build();
    ASSERT_TRUE(built.has_value());
    if (!built)
        return;
    auto host = std::move(*built);
    reservation->close();
    std::atomic<bool> received{};
    std::atomic<bool> peer_closed{};
    auto collect = [&]() -> cnetmod::task<void>
    {
        auto peer = co_await cnetmod::async_accept(*collector_io, *listener);
        if (peer)
        {
            for (;;)
            {
                char bytes[4096];
                const auto read = co_await cnetmod::async_read(*collector_io, *peer,
                    cnetmod::mutable_buffer{bytes, sizeof(bytes)});
                if (!read || *read == 0)
                {
                    peer_closed.store(true);
                    break;
                }
                received.store(true);
            }
        }
        collector_io->stop();
    };
    cnetmod::spawn(*collector_io, collect());
    std::jthread collector([&]
        {
            collector_io->run();
        });
    bool submitted{};
    std::jthread stopper([&]
        {
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (host.state() != application::application_state::running && std::chrono::steady_clock::now() < limit)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            submitted = host.telemetry().submit_log({.body = "shutdown-test"});
            while (!received.load() && std::chrono::steady_clock::now() < limit)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            host.request_stop();
        });
    const auto started = std::chrono::steady_clock::now();
    const auto result = host.run();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    stopper.join();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (!peer_closed.load() && std::chrono::steady_clock::now() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    const bool closed_before_destruction = peer_closed.load();
    collector_io->stop();
    collector.join();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(submitted && received.load());
    ASSERT_TRUE(closed_before_destruction);
    ASSERT_TRUE(elapsed < std::chrono::seconds{2});
    ASSERT_TRUE(host.state() == application::application_state::stopped);
    ASSERT_TRUE(host.telemetry().statistics().failed_batches > 0);
}
