TEST(application_amqp091_failed_handshake_releases_transport_before_retry)
{
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
    cnetmod::amqp091::amqp091_client client{*io};
    unsigned completed = 0;
    auto finish = [&]
    {
        if (++completed == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        for (unsigned attempt = 0; attempt != 2; ++attempt)
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection.has_value());
            if (!connection)
                break;
            std::array<std::byte, 8> header{};
            std::size_t received = 0;
            while (received != header.size())
            {
                auto bytes = co_await cnetmod::async_read(*io, *connection,
                    cnetmod::mutable_buffer{header.data() + received, header.size() - received});
                if (!bytes || *bytes == 0)
                    break;
                received += *bytes;
            }
            ASSERT_EQ(received, header.size());
            const std::array<std::uint8_t, 12> unexpected_open_ok{
                1, 0, 0, 0, 0, 0, 4, 0, 10, 0, 41, 0xce};
            ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection,
                             cnetmod::const_buffer{unexpected_open_ok.data(), unexpected_open_ok.size()}))
                    .has_value());
            cnetmod::cancel_token cancellation;
            auto closed = co_await cnetmod::with_deadline(*io,
                cnetmod::deadline::after(std::chrono::milliseconds{250}),
                cnetmod::async_read(*io, *connection,
                    cnetmod::mutable_buffer{header.data(), header.size()}, cancellation),
                cancellation);
            if (closed)
                ASSERT_EQ(*closed, 0U);
            else
                ASSERT_EQ(closed.error(), cnetmod::make_error_code(cnetmod::errc::end_of_file));
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        for (unsigned attempt = 0; attempt != 2; ++attempt)
        {
            auto connected = co_await client.async_connect(options);
            ASSERT_FALSE(connected.has_value());
            if (!connected)
                ASSERT_TRUE(connected.error().code == cnetmod::amqp091::error_code::unexpected_frame);
            ASSERT_TRUE(client.state() == cnetmod::amqp091::connection_state::disconnected);
            // Leave the client alive until the peer has checked transport release.
            ASSERT_TRUE((co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{300})).has_value());
        }
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
