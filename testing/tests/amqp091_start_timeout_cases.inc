namespace {

void verify_amqp091_silent_peer_cancellation(bool externally_cancelled)
{
    namespace app = cnetmod::application;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener)
        return;
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    cnetmod::amqp091::connection_options options;
    options.endpoint.port = endpoint->port();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    app::task_supervisor supervisor{*io};
    app::amqp091_service service{*io, options, "silent",
        app::service_requirement::required, app::recovery_policy{}};
    cnetmod::cancel_token startup_cancellation;
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto connection = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(connection.has_value());
        if (connection)
        {
            cnetmod::cancel_token cancellation;
            const auto limit = cnetmod::deadline::after(std::chrono::seconds{2});
            std::array<std::byte, 64> bytes{};
            std::size_t received = 0;
            for (;;)
            {
                auto read = co_await cnetmod::with_deadline(*io, limit,
                    cnetmod::async_read(*io, *connection,
                        cnetmod::mutable_buffer{bytes.data(), bytes.size()}, cancellation),
                    cancellation);
                if (!read)
                {
                    ASSERT_EQ(read.error(), cnetmod::make_error_code(cnetmod::errc::end_of_file));
                    break;
                }
                if (*read == 0)
                    break;
                received += *read;
                if (externally_cancelled && received >= 8)
                    startup_cancellation.cancel();
            }
            ASSERT_EQ(received, 8U);
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        app::service_context context{*io, telemetry, supervisor, startup_cancellation,
            cnetmod::deadline::after(externally_cancelled
                    ? std::chrono::milliseconds{1500}
                    : std::chrono::milliseconds{200})};
        auto result = co_await service.start(context);
        ASSERT_FALSE(result.has_value());
        if (!result)
            ASSERT_EQ(result.error(), std::make_error_code(externally_cancelled ? std::errc::operation_canceled : std::errc::timed_out));
        ASSERT_TRUE(service.client().state() == cnetmod::amqp091::connection_state::disconnected);
        finish();
    };
    auto server = peer();
    auto operation = exercise();
    server.handle().resume();
    operation.handle().resume();
    io->run();
    server.handle().promise().result();
    operation.handle().promise().result();
}

} // namespace

TEST(application_amqp091_start_timeout_interrupts_silent_peer)
{
    verify_amqp091_silent_peer_cancellation(false);
}

TEST(application_amqp091_start_cancellation_interrupts_silent_peer)
{
    verify_amqp091_silent_peer_cancellation(true);
}
