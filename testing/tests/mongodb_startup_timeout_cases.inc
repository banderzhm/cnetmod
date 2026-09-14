TEST(mongodb_service_hello_timeout_cleans_up_and_allows_retry)
{
    for (unsigned scenario = 0; scenario < 3; ++scenario)
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
        options.connection.command_timeout = std::chrono::milliseconds{scenario == 0 ? 50 : 0};
        options.minimum_size = 1;
        options.maximum_size = 1;
        cnetmod::application::mongodb_service service{*io, options, "timeout",
            cnetmod::application::service_requirement::required, {}};
        auto settle = [&](auto& operation)
        {
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
                (void)io->poll();
            if (!operation.handle().done())
                std::terminate();
        };
        for (unsigned attempt = 0; attempt < 2; ++attempt)
        {
            std::optional<cnetmod::socket> peer;
            auto receive_hello = [&]() -> cnetmod::task<bool>
            {
                auto accepted = co_await cnetmod::async_accept(*io, *listener);
                if (!accepted)
                    co_return false;
                peer.emplace(std::move(*accepted));
                std::array<std::byte, 16> header;
                std::size_t received = 0;
                while (received < header.size())
                {
                    auto count = co_await cnetmod::async_read(*io, *peer,
                        cnetmod::mutable_buffer{header.data() + received, header.size() - received});
                    if (!count || *count == 0)
                        co_return false;
                    received += *count;
                }
                co_return true;
            };
            auto receiving = receive_hello();
            receiving.handle().resume();
            cnetmod::cancel_token cancellation;
            cnetmod::application::service_context context{*io, telemetry, supervisor,
                cancellation, cnetmod::deadline::after(std::chrono::milliseconds{scenario == 1 ? 100 : 3000})};
            auto starting = service.start(context);
            starting.handle().resume();
            settle(receiving);
            ASSERT_TRUE(receiving.handle().promise().result());
            if (scenario == 2)
            {
                std::thread cancelling{[&]()
                    {
                        cancellation.cancel();
                    }};
                cancelling.join();
            }
            settle(starting);
            auto outcome = starting.handle().promise().result();
            ASSERT_FALSE(outcome.has_value());
            ASSERT_EQ(outcome.error(), std::make_error_code(scenario == 2 ? std::errc::operation_canceled : std::errc::timed_out));
            ASSERT_EQ(cancellation.is_cancelled(), scenario != 0);
            ASSERT_FALSE(cancellation.pending_.load());
            ASSERT_EQ(service.pool().connecting_count(), 0U);
            ASSERT_EQ(service.pool().checked_out_count(), 0U);
            ASSERT_EQ(service.pool().size(), 0U);
            ASSERT_FALSE(supervisor.state("mongodb-maintenance:timeout").has_value());
            auto observe_disconnect = [&]() -> cnetmod::task<bool>
            {
                std::array<std::byte, 4096> remaining;
                std::size_t total = 0;
                while (total <= 65536)
                {
                    auto count = co_await cnetmod::async_read(*io, *peer,
                        cnetmod::mutable_buffer{remaining.data(), remaining.size()});
                    if (!count)
                        co_return count.error() == cnetmod::make_error_code(cnetmod::errc::end_of_file) ||
                            count.error() == std::errc::connection_reset ||
                            count.error() == std::errc::connection_aborted;
                    if (*count == 0)
                        co_return true;
                    total += *count;
                }
                co_return false;
            };
            auto disconnected = observe_disconnect();
            disconnected.handle().resume();
            settle(disconnected);
            ASSERT_TRUE(disconnected.handle().promise().result());
        }
    }
}
