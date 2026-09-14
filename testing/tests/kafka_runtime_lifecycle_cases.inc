TEST(kafka_consumer_close_joins_sleeping_maintenance)
{
    namespace application = cnetmod::application;
    namespace kafka = cnetmod::kafka;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener)
        return;
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    kafka::client_options options;
    options.bootstrap_servers.push_back({.host = "127.0.0.1", .port = endpoint->port()});
    const application::recovery_policy recovery{
        .initial_delay = std::chrono::milliseconds{10},
        .maximum_delay = std::chrono::milliseconds{20},
        .budget = std::chrono::seconds{2},
        .jitter = 0};
    auto service_owner = std::make_shared<application::kafka_service>(*io, options, "lifecycle", application::service_requirement::required, recovery);
    auto& service = *service_owner;
    auto& client = service.client();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::task_supervisor supervisor{*io};
    application::service_registry services;
    application::health_registry health{{.failures_before_down = 1, .successes_before_up = 2}};
    ASSERT_TRUE(services.manage(service_owner));
    ASSERT_TRUE(health.add(service_owner));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    cnetmod::cancel_token cancellation;
    application::service_context context{*io, telemetry, supervisor, cancellation, {}};
    unsigned finished = 0;
#ifdef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
    std::optional<std::chrono::steady_clock::time_point> rejected_probe;
#endif
    const auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        for (unsigned generation = 0; generation != 2; ++generation)
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection.has_value());
            if (!connection)
            {
                finish();
                co_return;
            }
            auto read_exact = [&](cnetmod::socket& source, std::span<std::byte> bytes) -> cnetmod::task<bool>
            {
                while (!bytes.empty())
                {
                    const auto read = co_await cnetmod::async_read(*io, source, cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                    if (!read || *read == 0)
                        co_return false;
                    bytes = bytes.subspan(*read);
                }
                co_return true;
            };
            const int exchanges =
#ifdef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
                generation == 0 ? 6 : 2;
#else
                2;
#endif
            for (int step = 0; step != exchanges; ++step)
            {
                std::array<std::byte, 4> prefix;
                if (!(co_await read_exact(*connection, prefix)))
                    break;
                kafka::protocol::decoder size_decoder{prefix};
                const auto size = size_decoder.int32();
                ASSERT_TRUE(size && *size >= 8 && *size < 4096);
                if (!size || *size < 8 || *size >= 4096)
                    break;
                kafka::bytes request(static_cast<std::size_t>(*size));
                if (!(co_await read_exact(*connection, request)))
                    break;
                kafka::protocol::decoder header{request};
                const auto api = header.int16();
                const auto version = header.int16();
                const auto correlation = header.int32();
                ASSERT_TRUE(api && version && correlation);
                if (!api || !version || !correlation)
                    break;
                ASSERT_EQ(*api, step == 0 ? 18 : 3);
                ASSERT_EQ(*version, 0);
                kafka::protocol::encoder response;
                response.int32(*correlation);
                if (step == 0)
                {
                    response.int16(0);
                    response.int32(1);
                    response.int16(3);
                    response.int16(0);
                    response.int16(0);
                }
#ifdef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
                else if (generation == 0 && step == 2)
                {
                    rejected_probe = std::chrono::steady_clock::now();
                    ASSERT_FALSE(health.ready());
                    // A correlation-only frame is a malformed Metadata response.
                }
#endif
                else
                {
#ifdef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
                    if (generation == 0 && step == 3)
                    {
                        ASSERT_TRUE(rejected_probe.has_value());
                        if (rejected_probe)
                            ASSERT_TRUE(std::chrono::steady_clock::now() - *rejected_probe >= std::chrono::milliseconds{8});
                        ASSERT_FALSE(health.ready());
                    }
#endif
                    response.int32(1);
                    response.int32(1);
                    response.string("127.0.0.1");
                    response.int32(endpoint->port());
                    response.int32(0);
                }
                const auto payload = std::move(response).take();
                kafka::protocol::encoder frame;
                frame.int32(static_cast<std::int32_t>(payload.size()));
                frame.raw(payload);
                const auto bytes = std::move(frame).take();
                ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{bytes.data(), bytes.size()})).has_value());
#ifdef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
                if (generation == 0 && step == 2)
                {
                    // Metadata refresh also tries the discovered broker connection.
                    auto fallback = co_await cnetmod::async_accept(*io, *listener);
                    ASSERT_TRUE(fallback.has_value());
                    if (fallback)
                    {
                        std::array<std::byte, 4> fallback_prefix;
                        ASSERT_TRUE(co_await read_exact(*fallback, fallback_prefix));
                        kafka::protocol::decoder length_decoder{fallback_prefix};
                        const auto length = length_decoder.int32();
                        ASSERT_TRUE(length && *length >= 8 && *length < 4096);
                        if (length && *length >= 8 && *length < 4096)
                        {
                            kafka::bytes fallback_request(static_cast<std::size_t>(*length));
                            ASSERT_TRUE(co_await read_exact(*fallback, fallback_request));
                            kafka::protocol::decoder fallback_header{fallback_request};
                            const auto fallback_api = fallback_header.int16();
                            const auto fallback_version = fallback_header.int16();
                            const auto fallback_correlation = fallback_header.int32();
                            ASSERT_TRUE(fallback_api && *fallback_api == 3);
                            ASSERT_TRUE(fallback_version && *fallback_version == 0);
                            ASSERT_TRUE(fallback_correlation.has_value());
                            if (fallback_correlation)
                            {
                                kafka::protocol::encoder rejected;
                                rejected.int32(4);
                                rejected.int32(*fallback_correlation);
                                const auto rejection = std::move(rejected).take();
                                ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *fallback,
                                                 cnetmod::const_buffer{rejection.data(), rejection.size()}))
                                        .has_value());
                                rejected_probe = std::chrono::steady_clock::now();
                            }
                        }
                    }
                }
#endif
            }
            std::array<std::byte, 1> tail;
            const auto ended = co_await cnetmod::async_read(*io, *connection, cnetmod::mutable_buffer{tail.data(), tail.size()});
            ASSERT_TRUE(!ended || *ended == 0);
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        const auto connected = co_await lifecycle.start();
        ASSERT_TRUE(connected.has_value());
        health.mark_running();
        const auto refreshed = co_await client.refresh_metadata();
        ASSERT_TRUE(refreshed.has_value());
        auto consumer = client.make_consumer({.heartbeat_interval = std::chrono::seconds{5}, .enable_auto_commit = false});
        ASSERT_TRUE(consumer.has_value());
        if (consumer)
        {
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{5});
            auto pending_assignment = consumer->assign({});
            const auto started = std::chrono::steady_clock::now();
            ASSERT_TRUE((co_await consumer->close()).has_value());
            ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{150});
            ASSERT_FALSE(client.requires_async_close());
            const auto sought = co_await consumer->seek({"closed-topic", 0}, 0);
            ASSERT_FALSE(sought.has_value());
            if (!sought)
                ASSERT_TRUE(sought.error().code == kafka::error_code::configuration);
            const auto assigned = co_await consumer->assign({});
            ASSERT_FALSE(assigned.has_value());
            if (!assigned)
                ASSERT_TRUE(assigned.error().code == kafka::error_code::configuration);
            ASSERT_FALSE((co_await pending_assignment).has_value());
            ASSERT_FALSE((co_await consumer->subscribe({})).has_value());
            ASSERT_FALSE((co_await consumer->poll()).has_value());
            kafka::consumed_record record;
            ASSERT_FALSE((co_await consumer->commit(record)).has_value());
            ASSERT_TRUE((co_await consumer->close()).has_value());
        }
        auto retained_consumer = client.make_consumer({.heartbeat_interval = std::chrono::seconds{5}, .enable_auto_commit = false});
        ASSERT_TRUE(retained_consumer.has_value());
#ifdef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
        fail_after = 0U;
        bool escaped = false;
        try
        {
            io->poll();
        }
        catch (const std::bad_alloc&)
        {
            escaped = true;
        }
        const bool injected = !fail_after.has_value();
        fail_after.reset();
        ASSERT_TRUE(injected);
        ASSERT_FALSE(escaped);
        const auto maintenance_failure = client.background_error();
        ASSERT_TRUE(static_cast<bool>(maintenance_failure));
        const auto failed_health = co_await service.probe(context);
        ASSERT_TRUE(failed_health.status == application::service_health::down);
        ASSERT_EQ(failed_health.error, maintenance_failure);
        fail_after = 0U;
        const auto failed_restart = client.restart_failed_maintenance();
        fail_after.reset();
        ASSERT_FALSE(failed_restart.has_value());
        if (!failed_restart)
            ASSERT_EQ(failed_restart.error(), std::make_error_code(std::errc::not_enough_memory));
        ASSERT_EQ(client.background_error(), maintenance_failure);
        health.update(service.key(), failed_health);
        ASSERT_FALSE(health.ready());
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
        ASSERT_TRUE((co_await supervisor.join()).has_value());
        ASSERT_TRUE(rejected_probe.has_value());
        ASSERT_FALSE(health.ready());
        ASSERT_EQ(health.snapshots().front().consecutive_successes, 0U);
        co_await health.refresh(context);
        ASSERT_FALSE(health.ready());
        ASSERT_EQ(health.snapshots().front().consecutive_successes, 1U);
        co_await health.refresh(context);
        ASSERT_TRUE(health.ready());
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
#endif
        ASSERT_FALSE(static_cast<bool>(client.background_error()));
        ASSERT_TRUE(client.restart_failed_maintenance().has_value());
        {
            auto released_consumer = client.make_consumer({.heartbeat_interval = std::chrono::seconds{5}, .enable_auto_commit = false});
            ASSERT_TRUE(released_consumer.has_value());
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{5});
        }
        const auto shutdown_started = std::chrono::steady_clock::now();
        ASSERT_TRUE(client.requires_async_close());
        ASSERT_TRUE((co_await service.stop(context)).has_value());
        ASSERT_FALSE(client.requires_async_close());
        ASSERT_TRUE((co_await client.async_close()).has_value());
        ASSERT_FALSE(static_cast<bool>(client.background_error()));
        ASSERT_TRUE(std::chrono::steady_clock::now() - shutdown_started < std::chrono::milliseconds{150});
        if (retained_consumer)
            ASSERT_FALSE((co_await retained_consumer->poll()).has_value());
        ASSERT_FALSE(client.make_consumer({}).has_value());
        ASSERT_FALSE(client.make_producer().has_value());
        ASSERT_FALSE(client.restart_failed_maintenance().has_value());
        const auto stopped_refresh = co_await client.refresh_metadata();
        ASSERT_FALSE(stopped_refresh.has_value());
        if (!stopped_refresh)
            ASSERT_TRUE(stopped_refresh.error().code == kafka::error_code::configuration);
        const auto stopped_metadata = client.metadata();
        cnetmod::cancel_token cancelled_restart;
        cancelled_restart.cancel();
        ASSERT_FALSE((co_await client.connect(&cancelled_restart)).has_value());
        ASSERT_TRUE(client.metadata() != stopped_metadata);
        ASSERT_TRUE(client.api_versions().empty());
        client.close();
        ASSERT_TRUE((co_await service.start(context)).has_value());
        ASSERT_TRUE((co_await client.refresh_metadata()).has_value());
        ASSERT_TRUE(client.metadata() != stopped_metadata);
        ASSERT_FALSE(client.api_versions().empty());
        auto restarted_consumer = client.make_consumer({.enable_auto_commit = false});
        ASSERT_TRUE(restarted_consumer.has_value());
        if (restarted_consumer)
            ASSERT_TRUE((co_await restarted_consumer->assign({})).has_value());
        if (retained_consumer)
        {
            ASSERT_FALSE((co_await retained_consumer->assign({})).has_value());
            ASSERT_FALSE((co_await retained_consumer->poll()).has_value());
        }
#ifdef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
        fail_after = 0U;
        io->poll();
        const bool shutdown_fault_injected = !fail_after.has_value();
        fail_after.reset();
        ASSERT_TRUE(shutdown_fault_injected);
        ASSERT_TRUE(static_cast<bool>(client.background_error()));
        const auto faulted_stop = co_await service.stop(context);
        ASSERT_FALSE(faulted_stop.has_value());
        ASSERT_FALSE(client.requires_async_close());
#endif
        ASSERT_TRUE((co_await lifecycle.stop()).has_value());
        ASSERT_EQ(lifecycle.active_service_count(), std::size_t{0});
        ASSERT_FALSE(client.requires_async_close());
        client.close();
        finish();
    };
    auto serving = peer();
    auto running = exercise();
    serving.handle().resume();
    running.handle().resume();
    io->run();
    ASSERT_TRUE(serving.handle().done() && running.handle().done());
    serving.handle().promise().result();
    running.handle().promise().result();
}
