namespace {

void verify_amqp091_start_admission(bool cancelled, bool expired)
{
    namespace app = cnetmod::application;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto reserved = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(reserved.has_value());
    if (!reserved)
        return;
    ASSERT_TRUE(reserved->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reserved->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    cnetmod::amqp091::connection_options options;
    options.endpoint.port = endpoint->port();
    options.endpoint.connect_timeout = std::chrono::milliseconds{100};
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    app::task_supervisor supervisor{*io};
    app::amqp091_service service{*io, options, "admission",
        app::service_requirement::required, app::recovery_policy{}};
    cnetmod::cancel_token cancellation;
    if (cancelled)
        cancellation.cancel();
    app::service_context context{*io, telemetry, supervisor, cancellation,
        expired ? cnetmod::deadline::after(std::chrono::milliseconds{0}) : cnetmod::deadline{}};
    bool completed_inline = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto result = co_await service.start(context);
        ASSERT_FALSE(result.has_value());
        if (!result)
            ASSERT_EQ(result.error(), std::make_error_code(cancelled ? std::errc::operation_canceled : std::errc::timed_out));
        ASSERT_TRUE(service.client().state() == cnetmod::amqp091::connection_state::disconnected);
        completed_inline = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    const bool admitted_without_io = completed_inline;
    if (!operation.handle().done())
        io->run();
    operation.handle().promise().result();
    ASSERT_TRUE(admitted_without_io);
}

} // namespace

TEST(application_amqp091_start_rejects_precancellation)
{
    verify_amqp091_start_admission(true, false);
}

TEST(application_amqp091_start_rejects_expired_deadline)
{
    verify_amqp091_start_admission(false, true);
}

TEST(application_amqp091_start_prioritizes_precancellation)
{
    verify_amqp091_start_admission(true, true);
}
