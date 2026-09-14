TEST(mongodb_service_start_preserves_failure_category)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::cancel_token cancellation;
    cnetmod::application::service_context context{*io, telemetry, supervisor,
        cancellation, cnetmod::deadline::after(std::chrono::seconds{3})};
    for (bool closed : {false, true})
    {
        cnetmod::mongodb::connection_pool_options options;
        options.connection.host.clear();
        options.connection.password = "must-not-appear-in-error";
        options.minimum_size = 1;
        cnetmod::application::mongodb_service service{*io, options, "failure",
            cnetmod::application::service_requirement::required, {}};
        if (closed)
            service.pool().close();
        auto starting = service.start(context);
        starting.handle().resume();
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!starting.handle().done() && std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        if (!starting.handle().done())
            std::terminate();
        auto outcome = starting.handle().promise().result();
        ASSERT_FALSE(outcome.has_value());
        ASSERT_TRUE(static_cast<bool>(outcome.error()));
        ASSERT_EQ(std::string{outcome.error().category().name()}, std::string{"cnetmod.mongodb"});
        auto expected = closed ? cnetmod::mongodb::error_code::connection_closed
                               : cnetmod::mongodb::error_code::connection_failed;
        ASSERT_EQ(outcome.error().value(), static_cast<int>(expected) + 1);
        ASSERT_TRUE(outcome.error() == (closed ? std::errc::not_connected : std::errc::io_error));
        ASSERT_EQ(outcome.error().message(), std::format("MongoDB error {}", outcome.error().value()));
        ASSERT_EQ(service.pool().connecting_count(), 0U);
        ASSERT_EQ(service.pool().size(), 0U);
        ASSERT_FALSE(supervisor.state("mongodb-maintenance:failure").has_value());
    }
}

namespace {
enum class mongodb_shutdown_case
{
    delayed_return,
    deadline,
    cancellation,
    startup_cancellation,
    supervision_rejected
};

void verify_mongodb_service_shutdown(mongodb_shutdown_case scenario)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    std::optional<cnetmod::socket> peer;
    cnetmod::cancel_token startup;
    auto handshake = [&]() -> cnetmod::task<bool>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        if (!accepted)
            co_return false;
        peer.emplace(std::move(*accepted));
        std::vector<std::byte> request(16);
        std::size_t received = 0;
        for (unsigned phase = 0; phase < 2; ++phase)
        {
            while (received < request.size())
            {
                auto count = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{request.data() + received, request.size() - received});
                if (!count || *count == 0)
                    co_return false;
                received += *count;
            }
            if (phase == 0)
            {
                cnetmod::buffer_reader reader{cnetmod::const_buffer{request.data(), request.size()}};
                auto length = reader.read_u32_le();
                if (!length || *length < 16 || *length > 65536)
                    co_return false;
                request.resize(*length);
            }
        }
        auto reply = cnetmod::mongodb::encode_command_message(1,
            {{"ok", 1.0}, {"maxWireVersion", std::int32_t{17}}, {"isWritablePrimary", true}}, 65536);
        if (!reply)
            co_return false;
        std::copy_n(request.begin() + 4, 4, reply->begin() + 8);
        co_return (co_await cnetmod::async_write_all(*io, *peer,
                       cnetmod::const_buffer{reply->data(), reply->size()}))
            .has_value();
    };
    auto settle = [&](auto& operation)
    {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        if (!operation.handle().done())
            std::terminate();
    };
    auto server = handshake();
    server.handle().resume();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::mongodb::connection_pool_options options;
    options.connection.port = listener->local_endpoint()->port();
    options.minimum_size = 1;
    options.maximum_size = 1;
    cnetmod::application::mongodb_service service{*io, options, "shutdown",
        cnetmod::application::service_requirement::required, {}};
    cnetmod::application::service_context start_context{*io, telemetry, supervisor,
        startup, cnetmod::deadline::after(std::chrono::seconds{3})};
    auto acquire_before_cancellation = [&]() -> cnetmod::task<cnetmod::mongodb::result<cnetmod::mongodb::pooled_connection>>
    {
        auto lease = co_await service.pool().acquire();
        if (scenario == mongodb_shutdown_case::startup_cancellation && lease)
            startup.cancel();
        co_return std::move(lease);
    };
    auto acquire = acquire_before_cancellation();
    if (scenario == mongodb_shutdown_case::supervision_rejected)
        supervisor.request_stop();
    else
    {
        acquire.handle().resume();
        ASSERT_EQ(service.pool().connecting_count(), 1U);
    }
    auto starting = service.start(start_context);
    starting.handle().resume();
    ASSERT_FALSE(starting.handle().done());
    if (scenario != mongodb_shutdown_case::supervision_rejected)
        settle(acquire);
    settle(starting);
    auto started = starting.handle().promise().result();
    if (scenario == mongodb_shutdown_case::supervision_rejected)
    {
        settle(server);
        ASSERT_TRUE(server.handle().promise().result());
        ASSERT_FALSE(started.has_value());
        ASSERT_EQ(started.error(), std::make_error_code(std::errc::operation_canceled));
        ASSERT_EQ(service.pool().checked_out_count(), 0U);
        ASSERT_EQ(service.pool().idle_count(), 0U);
        ASSERT_FALSE(supervisor.state("mongodb-maintenance:shutdown").has_value());
        auto rejected = service.pool().acquire();
        rejected.handle().resume();
        ASSERT_TRUE(rejected.handle().done());
        ASSERT_FALSE(rejected.handle().promise().result().has_value());
        return;
    }
    if (scenario == mongodb_shutdown_case::startup_cancellation)
    {
        ASSERT_FALSE(started.has_value());
        ASSERT_EQ(started.error(), std::make_error_code(std::errc::operation_canceled));
    }
    else
        ASSERT_TRUE(started.has_value());
    settle(server);
    ASSERT_TRUE(server.handle().promise().result());
    auto lease = acquire.handle().promise().result();
    ASSERT_TRUE(lease.has_value());
    if (scenario == mongodb_shutdown_case::startup_cancellation)
    {
        ASSERT_FALSE(supervisor.state("mongodb-maintenance:shutdown").has_value());
        ASSERT_EQ(service.pool().checked_out_count(), 1U);
        *lease = cnetmod::mongodb::pooled_connection{};
        cnetmod::cancel_token cleanup;
        cnetmod::application::service_context cleanup_context{*io, telemetry, supervisor,
            cleanup, cnetmod::deadline::after(std::chrono::seconds{1})};
        auto closing = service.stop(cleanup_context);
        closing.handle().resume();
        settle(closing);
        ASSERT_TRUE(closing.handle().promise().result().has_value());
        ASSERT_EQ(service.pool().size(), 0U);
        return;
    }

    cnetmod::cancel_token cancellation;
    cnetmod::application::service_context stop_context{*io, telemetry, supervisor,
        cancellation, cnetmod::deadline::after(scenario == mongodb_shutdown_case::deadline ? std::chrono::milliseconds{10} : std::chrono::milliseconds{1000})};
    auto stopping = service.stop(stop_context);
    stopping.handle().resume();
    ASSERT_FALSE(stopping.handle().done());
    ASSERT_EQ(service.pool().checked_out_count(), 1U);
    ASSERT_TRUE(lease->get().is_open());
    if (scenario == mongodb_shutdown_case::delayed_return)
        *lease = cnetmod::mongodb::pooled_connection{};
    else if (scenario == mongodb_shutdown_case::cancellation)
        cancellation.cancel();
    settle(stopping);
    auto stopped = stopping.handle().promise().result();
    if (scenario == mongodb_shutdown_case::delayed_return)
        ASSERT_TRUE(stopped.has_value());
    else
    {
        ASSERT_FALSE(stopped.has_value());
        ASSERT_EQ(stopped.error(), std::make_error_code(scenario == mongodb_shutdown_case::deadline ? std::errc::timed_out : std::errc::operation_canceled));
        ASSERT_EQ(service.pool().checked_out_count(), 1U);
        ASSERT_TRUE(lease->get().is_open());
        cnetmod::cancel_token admission;
        cnetmod::application::service_context admission_context{*io, telemetry, supervisor,
            admission, cnetmod::deadline::after(std::chrono::seconds{1})};
        auto restart = service.start(admission_context);
        restart.handle().resume();
        ASSERT_TRUE(restart.handle().done());
        auto restarted = restart.handle().promise().result();
        ASSERT_FALSE(restarted.has_value());
        ASSERT_EQ(restarted.error(), std::make_error_code(std::errc::operation_in_progress));
        auto probe = service.probe(admission_context);
        probe.handle().resume();
        ASSERT_TRUE(probe.handle().done());
        ASSERT_TRUE(probe.handle().promise().result().status == cnetmod::application::service_health::stopping);
        *lease = cnetmod::mongodb::pooled_connection{};
    }
    cnetmod::cancel_token retry_token;
    cnetmod::application::service_context retry_context{*io, telemetry, supervisor,
        retry_token, cnetmod::deadline::after(std::chrono::seconds{1})};
    auto retry = service.stop(retry_context);
    retry.handle().resume();
    settle(retry);
    ASSERT_TRUE(retry.handle().promise().result().has_value());
    ASSERT_EQ(service.pool().checked_out_count(), 0U);
    ASSERT_EQ(service.pool().size(), 0U);
    for (unsigned cycle = 0; cycle < 2; ++cycle)
    {
        auto* previous_pool = &service.pool();
        auto next_server = handshake();
        next_server.handle().resume();
        start_context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{3});
        auto restarting = service.start(start_context);
        restarting.handle().resume();
        settle(restarting);
        settle(next_server);
        ASSERT_TRUE(next_server.handle().promise().result());
        ASSERT_TRUE(restarting.handle().promise().result().has_value());
        ASSERT_TRUE(&service.pool() != previous_pool);
        verify_successful_mongodb_probe(service, *io, *peer, telemetry, supervisor);
        for (bool cancelled : {false, true})
        {
            cnetmod::cancel_token probe_token;
            if (cancelled)
                probe_token.cancel();
            cnetmod::application::service_context probe_context{*io, telemetry, supervisor,
                probe_token, cancelled ? cnetmod::deadline::after(std::chrono::seconds{1}) : cnetmod::deadline::after(std::chrono::milliseconds{0})};
            auto probing = service.probe(probe_context);
            probing.handle().resume();
            ASSERT_TRUE(probing.handle().done());
            auto report = probing.handle().promise().result();
            ASSERT_TRUE(report.status == cnetmod::application::service_health::down);
            ASSERT_EQ(report.error, std::make_error_code(cancelled ? std::errc::operation_canceled : std::errc::timed_out));
            ASSERT_EQ(service.pool().idle_count(), 1U);
            ASSERT_EQ(service.pool().checked_out_count(), 0U);
        }
        if (cycle == 1)
        {
            cnetmod::cancel_token probe_token;
            const bool cancel_probe = scenario == mongodb_shutdown_case::cancellation;
            cnetmod::application::service_context probe_context{*io, telemetry, supervisor,
                probe_token, cnetmod::deadline::after(cancel_probe ? std::chrono::milliseconds{1000} : std::chrono::milliseconds{10})};
            auto probing = service.probe(probe_context);
            probing.handle().resume();
            ASSERT_FALSE(probing.handle().done());
            if (cancel_probe)
            {
                std::jthread requester{[&]()
                    {
                        probe_token.cancel();
                    }};
                requester.join();
            }
            settle(probing);
            auto report = probing.handle().promise().result();
            ASSERT_TRUE(report.status == cnetmod::application::service_health::down);
            ASSERT_EQ(report.error, std::make_error_code(cancel_probe ? std::errc::operation_canceled : std::errc::timed_out));
            ASSERT_EQ(service.pool().checked_out_count(), 0U);
            ASSERT_EQ(service.pool().size(), 0U);
            ASSERT_FALSE(probe_token.pending_.load());
            retry_context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{1});
            auto stopping_after_probe = service.stop(retry_context);
            stopping_after_probe.handle().resume();
            settle(stopping_after_probe);
            ASSERT_TRUE(stopping_after_probe.handle().promise().result().has_value());
            continue;
        }
        auto borrowing = service.pool().acquire();
        borrowing.handle().resume();
        settle(borrowing);
        auto borrowed = borrowing.handle().promise().result();
        ASSERT_TRUE(borrowed.has_value());
        ASSERT_TRUE(borrowed->get().is_open());
        cnetmod::cancel_token ping_token;
        ping_token.cancel();
        auto rejected_ping = borrowed->get().ping(ping_token);
        rejected_ping.handle().resume();
        ASSERT_TRUE(rejected_ping.handle().done());
        auto rejected_result = rejected_ping.handle().promise().result();
        ASSERT_FALSE(rejected_result.has_value());
        ASSERT_TRUE(rejected_result.error().code == cnetmod::mongodb::error_code::operation_cancelled);
        ASSERT_TRUE(borrowed->get().is_open());
        ping_token.reset();
        auto pending_ping = borrowed->get().ping(ping_token);
        pending_ping.handle().resume();
        ASSERT_FALSE(pending_ping.handle().done());
        std::jthread requester{[&]()
            {
                ping_token.cancel();
            }};
        requester.join();
        settle(pending_ping);
        auto ping_result = pending_ping.handle().promise().result();
        ASSERT_FALSE(ping_result.has_value());
        ASSERT_TRUE(ping_result.error().code == cnetmod::mongodb::error_code::operation_cancelled);
        ASSERT_FALSE(borrowed->get().is_open());
        ASSERT_FALSE(ping_token.pending_.load());
        *borrowed = cnetmod::mongodb::pooled_connection{};
        retry_context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{1});
        auto stopping_again = service.stop(retry_context);
        stopping_again.handle().resume();
        settle(stopping_again);
        ASSERT_TRUE(stopping_again.handle().promise().result().has_value());
        ASSERT_EQ(service.pool().checked_out_count(), 0U);
    }
    supervisor.request_stop();
    auto joined = supervisor.join();
    joined.handle().resume();
    settle(joined);
    ASSERT_TRUE(joined.handle().promise().result().has_value());
}
} // namespace

TEST(mongodb_service_stop_waits_for_borrower_return)
{
    verify_mongodb_service_shutdown(mongodb_shutdown_case::delayed_return);
}

TEST(mongodb_service_stop_deadline_preserves_cleanup_for_retry)
{
    verify_mongodb_service_shutdown(mongodb_shutdown_case::deadline);
}

TEST(mongodb_service_stop_cancellation_preserves_cleanup_for_retry)
{
    verify_mongodb_service_shutdown(mongodb_shutdown_case::cancellation);
}

TEST(mongodb_service_late_startup_cancellation_does_not_register_maintenance)
{
    verify_mongodb_service_shutdown(mongodb_shutdown_case::startup_cancellation);
}

TEST(mongodb_service_registration_failure_closes_warmed_pool)
{
    verify_mongodb_service_shutdown(mongodb_shutdown_case::supervision_rejected);
}

TEST(mongodb_service_stop_retains_pending_connection_attempt)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::mongodb::connection_pool_options options;
    options.connection.port = listener->local_endpoint()->port();
    options.connection.connect_timeout = std::chrono::seconds{30};
    options.connection.command_timeout = std::chrono::milliseconds{0};
    options.minimum_size = 1;
    options.maximum_size = 1;
    cnetmod::application::mongodb_service service{*io, options, "connecting",
        cnetmod::application::service_requirement::required, {}};
    // The listening peer deliberately supplies no hello response.
    auto connecting = service.pool().acquire();
    connecting.handle().resume();
    ASSERT_EQ(service.pool().connecting_count(), 1U);
    ASSERT_EQ(service.pool().checked_out_count(), 0U);
    auto warming = service.pool().warm_up();
    warming.handle().resume();
    ASSERT_FALSE(warming.handle().done());
    cnetmod::cancel_token cancellation;
    cnetmod::application::service_context context{*io, telemetry, supervisor,
        cancellation, cnetmod::deadline::after(std::chrono::milliseconds{0})};
    auto stopping = service.stop(context);
    stopping.handle().resume();
    ASSERT_TRUE(stopping.handle().done());
    auto stopped = stopping.handle().promise().result();
    ASSERT_FALSE(stopped.has_value());
    ASSERT_EQ(stopped.error(), std::make_error_code(std::errc::timed_out));
    ASSERT_EQ(service.pool().connecting_count(), 1U);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while ((!connecting.handle().done() || !warming.handle().done()) && std::chrono::steady_clock::now() < limit)
        (void)io->poll();
    if (!connecting.handle().done() || !warming.handle().done())
        std::terminate();
    auto warmed = warming.handle().promise().result();
    ASSERT_FALSE(warmed.has_value());
    ASSERT_TRUE(warmed.error().code == cnetmod::mongodb::error_code::connection_closed);
    ASSERT_FALSE(connecting.handle().promise().result().has_value());
    ASSERT_EQ(service.pool().connecting_count(), 0U);
    context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{1});
    auto retry = service.stop(context);
    retry.handle().resume();
    ASSERT_TRUE(retry.handle().done());
    ASSERT_TRUE(retry.handle().promise().result().has_value());
    ASSERT_EQ(service.pool().size(), 0U);
}
