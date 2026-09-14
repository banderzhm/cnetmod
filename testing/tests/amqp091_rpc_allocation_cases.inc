TEST(amqp091_rpc_allocation_failure_releases_registration)
{
    namespace amqp = cnetmod::amqp091;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener);
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}));
    ASSERT_TRUE(listener->listen());
    amqp::connection_options options;
    options.endpoint.port = listener->local_endpoint()->port();
    amqp::amqp091_client client{*io};
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto connection = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(connection);
        std::vector<std::byte> wire;
        auto append = [&](std::uint16_t cls, std::uint16_t method, std::initializer_list<unsigned char> arguments)
        {
            amqp::method_frame reply{.channel = static_cast<std::uint16_t>(cls == 10 ? 0 : 1), .class_id = cls, .method_id = method};
            for (auto byte : arguments)
                reply.arguments.push_back(static_cast<std::byte>(byte));
            auto frame = amqp::encode_method(reply);
            auto bytes = amqp::encode_frame(*frame);
            wire.insert(wire.end(), bytes->begin(), bytes->end());
        };
        append(10, 10, {0, 9, 0, 0, 0, 0, 0, 0, 0, 5, 'P', 'L', 'A', 'I', 'N', 0, 0, 0, 5, 'e', 'n', '_', 'U', 'S'});
        append(10, 30, {0, 0, 0, 0, 16, 0, 0, 0});
        append(10, 41, {0});
        append(20, 11, {0, 0, 0, 0});
        ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
        std::size_t protocol_bytes = 8;
        amqp::frame_parser parser;
        std::array<std::byte, 4096> bytes{};
        for (;;)
        {
            auto read = co_await cnetmod::async_read(*io, *connection, cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            if (!read || *read == 0)
                break;
            const auto skip = std::min(protocol_bytes, *read);
            protocol_bytes -= skip;
            auto frames = parser.feed(std::span<const std::byte>{bytes.data() + skip, *read - skip});
            ASSERT_TRUE(frames);
            for (const auto& frame : *frames)
            {
                auto method = amqp::decode_method(frame);
                ASSERT_TRUE(method);
                if (method->class_id == 60 && method->method_id == 10)
                {
                    wire.clear();
                    append(60, 11, {});
                    ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
                }
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await client.async_connect(options));
        auto channel = co_await client.async_open_channel();
        ASSERT_TRUE(channel);
        cnetmod::cancel_token cancellation;
        auto pump = client.async_run(cancellation);
        pump.handle().resume();
        unsigned failures = 0;
        bool reached_io = false;
        for (std::size_t position = 0; position != 64 && !reached_io; ++position)
        {
            auto operation = (*channel)->async_set_qos({.prefetch_count = 1});
            fail_after = position;
            operation.handle().resume();
            const bool injected = !fail_after;
            fail_after.reset();
            if (injected)
                ++failures;
            else
                reached_io = true;
            while (!operation.handle().done())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            try
            {
                auto result = operation.handle().promise().result();
                if (!injected)
                    ASSERT_TRUE(result);
            }
            catch (const std::bad_alloc&)
            {
                ASSERT_TRUE(injected);
            }
            // Every injected failure must leave the same channel reusable.
            ASSERT_TRUE(co_await (*channel)->async_set_qos({.prefetch_count = 2}));
        }
        ASSERT_TRUE(reached_io);
        ASSERT_GT(failures, 4U);
        cancellation.cancel();
        while (!pump.handle().done())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        (void)pump.handle().promise().result();
        ASSERT_TRUE(co_await client.async_close());
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
