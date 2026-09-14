TEST(mongodb_startup_allocation_failure_allows_cleanup_and_restart)
{
    cnetmod::net_init network;
    unsigned failures = 0;
    unsigned successes = 0;
    for (unsigned attempt = 0; attempt < 64; ++attempt)
    {
        const auto allocation = static_cast<std::size_t>(attempt % 32);
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        cnetmod::application::task_supervisor supervisor{*io};
        cnetmod::application::mongodb_service service{*io, {}, "allocation",
            cnetmod::application::service_requirement::required, {}};
        cnetmod::cancel_token cancellation;
        cnetmod::application::service_context context{*io, telemetry, supervisor,
            cancellation, cnetmod::deadline::after(std::chrono::seconds{2})};
        auto settle = [&](auto& operation)
        {
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
                (void)io->poll();
            if (!operation.handle().done())
                std::terminate();
        };
        if (attempt >= 32)
        {
            auto initial = service.start(context);
            initial.handle().resume();
            settle(initial);
            ASSERT_TRUE(initial.handle().promise().result().has_value());
            auto initial_stop = service.stop(context);
            initial_stop.handle().resume();
            settle(initial_stop);
            ASSERT_TRUE(initial_stop.handle().promise().result().has_value());
        }
        context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{2});
        auto starting = service.start(context);
        fail_after = allocation;
        starting.handle().resume();
        fail_after.reset();
        settle(starting);
        try
        {
            auto started = starting.handle().promise().result();
            if (started)
                ++successes;
            else
                ++failures;
        }
        catch (const std::bad_alloc&)
        {
            ++failures;
        }
        context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{2});
        auto closing = service.stop(context);
        closing.handle().resume();
        settle(closing);
        ASSERT_TRUE(closing.handle().promise().result().has_value());
        ASSERT_EQ(service.pool().checked_out_count(), 0U);
        ASSERT_EQ(service.pool().connecting_count(), 0U);
        auto restarting = service.start(context);
        restarting.handle().resume();
        settle(restarting);
        ASSERT_TRUE(restarting.handle().promise().result().has_value());
        auto stopping = service.stop(context);
        stopping.handle().resume();
        settle(stopping);
        ASSERT_TRUE(stopping.handle().promise().result().has_value());
        supervisor.request_stop();
        auto joined = supervisor.join();
        joined.handle().resume();
        settle(joined);
        ASSERT_TRUE(joined.handle().promise().result().has_value());
    }
    ASSERT_TRUE(failures > 0);
    ASSERT_TRUE(successes > 0);
}
