TEST(tls_shutdown_deadline_cancels_a_silent_peer)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto identity = cnetmod::ssl_context::server();
    ASSERT_TRUE(identity.has_value());
    auto certificate = identity->load_cert_file(CNETMOD_TLS_TEST_CERT);
    auto key = identity->load_key_file(CNETMOD_TLS_TEST_KEY);
    ASSERT_TRUE(certificate.has_value());
    ASSERT_TRUE(key.has_value());
    if (!certificate || !key)
        return;
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    bool received_shutdown_bytes = false;
    bool observed_eof = false;
    cnetmod::async_semaphore peer_handshake_complete{0};
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto socket = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(socket.has_value());
        cnetmod::ssl_stream stream{*identity, *io, *socket};
        stream.set_accept_state();
        ASSERT_TRUE((co_await stream.async_handshake()).has_value());
        peer_handshake_complete.release();
        // Consume ciphertext without SSL_read/SSL_shutdown: deliberately never
        // acknowledge the client's close_notify, keeping shutdown pending.
        std::array<std::byte, 4096> bytes{};
        for (;;)
        {
            auto received = co_await cnetmod::async_read(*io, *socket,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            if (!received || *received == 0)
            {
                observed_eof = true;
                break;
            }
            received_shutdown_bytes = true;
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto socket = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(socket.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*io, *socket, *endpoint)).has_value());
        auto context = cnetmod::ssl_context::client();
        ASSERT_TRUE(context.has_value());
        context->set_verify_peer(false);
        cnetmod::ssl_stream stream{*context, *io, *socket};
        stream.set_connect_state();
        ASSERT_TRUE((co_await stream.async_handshake()).has_value());
        co_await peer_handshake_complete.acquire();
        cnetmod::cancel_token cancellation;
        const auto started = std::chrono::steady_clock::now();
        auto result = co_await cnetmod::with_timeout(*io, std::chrono::milliseconds{30},
            stream.async_shutdown(cancellation), cancellation);
        ASSERT_FALSE(result.has_value());
        if (!result)
            ASSERT_EQ(result.error(), std::make_error_code(std::errc::timed_out));
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
        socket->close();
        finish();
    };
    auto peer_task = peer();
    auto exercise_task = exercise();
    io->post(peer_task.handle());
    io->post(exercise_task.handle());
    io->run();
    ASSERT_TRUE(peer_task.handle().done());
    ASSERT_TRUE(exercise_task.handle().done());
    peer_task.handle().promise().result();
    exercise_task.handle().promise().result();
    ASSERT_TRUE(received_shutdown_bytes);
    ASSERT_TRUE(observed_eof);
}

TEST(tls_memory_bio_supports_concurrent_read_and_serialized_write)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto identity = cnetmod::ssl_context::server();
    ASSERT_TRUE(identity.has_value());
    ASSERT_TRUE(identity->load_cert_file(CNETMOD_TLS_TEST_CERT).has_value());
    ASSERT_TRUE(identity->load_key_file(CNETMOD_TLS_TEST_KEY).has_value());
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(
                            {cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    constexpr std::size_t rounds = 256;
    constexpr std::size_t payload_size = 4096;
    const std::string payload(payload_size, 'x');
    unsigned finished{};
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto socket = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(socket.has_value());
        cnetmod::ssl_stream stream{*identity, *io, *socket};
        stream.set_accept_state();
        ASSERT_TRUE((co_await stream.async_handshake()).has_value());
        auto write = [&]() -> cnetmod::task<void>
        {
            for (std::size_t index = 0; index < rounds; ++index)
                ASSERT_TRUE((co_await stream.async_write_all(
                                 cnetmod::const_buffer{payload.data(), payload.size()}))
                        .has_value());
        };
        auto read = [&]() -> cnetmod::task<void>
        {
            std::array<std::byte, 1> marker{};
            auto received = co_await stream.async_read(cnetmod::buffer(marker));
            ASSERT_TRUE(received.has_value());
            if (received)
                ASSERT_EQ(*received, std::size_t{1});
        };
        co_await cnetmod::when_all(write(), read());
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto socket = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(socket.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(
                         *io, *socket, *endpoint))
                .has_value());
        auto context = cnetmod::ssl_context::client();
        ASSERT_TRUE(context.has_value());
        context->set_verify_peer(false);
        cnetmod::ssl_stream stream{*context, *io, *socket};
        stream.set_connect_state();
        ASSERT_TRUE((co_await stream.async_handshake()).has_value());
        std::array<std::byte, 16U * 1024U> buffer{};
        std::size_t total{};
        while (total < rounds * payload_size)
        {
            auto received = co_await stream.async_read(cnetmod::buffer(buffer));
            ASSERT_TRUE(received.has_value());
            if (!received || *received == 0)
                break;
            total += *received;
        }
        ASSERT_EQ(total, rounds * payload_size);
        const std::array marker{std::byte{1}};
        ASSERT_TRUE((co_await stream.async_write_all(
                         cnetmod::const_buffer{marker.data(), marker.size()}))
                .has_value());
        finish();
    };
    auto peer_task = peer();
    auto exercise_task = exercise();
    io->post(peer_task.handle());
    io->post(exercise_task.handle());
    io->run();
    ASSERT_TRUE(peer_task.handle().done());
    ASSERT_TRUE(exercise_task.handle().done());
    peer_task.handle().promise().result();
    exercise_task.handle().promise().result();
}

#if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
TEST(tls_kernel_tx_offload_preserves_payload_and_close_notify)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto identity = cnetmod::ssl_context::server();
    ASSERT_TRUE(identity.has_value());
    identity->set_kernel_tls(true);
    ASSERT_TRUE(identity->load_cert_file(CNETMOD_TLS_TEST_CERT).has_value());
    ASSERT_TRUE(identity->load_key_file(CNETMOD_TLS_TEST_KEY).has_value());

    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(
                            {cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    bool offload_active = false;
    unsigned finished{};
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto socket = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(socket.has_value());
        cnetmod::ssl_stream stream{*identity, *io, *socket};
        stream.set_accept_state();
        ASSERT_TRUE((co_await stream.async_handshake()).has_value());
        offload_active = stream.kernel_tls_active();
        constexpr std::string_view payload = "ktls";
        ASSERT_TRUE((co_await stream.async_write_all(
                         cnetmod::const_buffer{payload.data(), payload.size()}))
                .has_value());
        ASSERT_TRUE((co_await stream.async_shutdown()).has_value());
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto socket = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(socket.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(
                         *io, *socket, *endpoint))
                .has_value());
        auto context = cnetmod::ssl_context::client();
        ASSERT_TRUE(context.has_value());
        context->set_verify_peer(false);
        cnetmod::ssl_stream stream{*context, *io, *socket};
        stream.set_connect_state();
        ASSERT_TRUE((co_await stream.async_handshake()).has_value());
        std::array<std::byte, 16U> bytes{};
        auto received = co_await stream.async_read(cnetmod::buffer(bytes));
        ASSERT_TRUE(received.has_value());
        ASSERT_EQ(*received, std::size_t{4});
        const auto text = std::string_view{
            reinterpret_cast<const char*>(bytes.data()), *received};
        ASSERT_EQ(text, std::string_view{"ktls"});
        auto closed = co_await stream.async_read(cnetmod::buffer(bytes));
        ASSERT_TRUE(closed.has_value());
        if (closed)
            ASSERT_EQ(*closed, std::size_t{0});
        finish();
    };

    auto peer_task = peer();
    auto exercise_task = exercise();
    io->post(peer_task.handle());
    io->post(exercise_task.handle());
    io->run();
    peer_task.handle().promise().result();
    exercise_task.handle().promise().result();
    // Kernels without TLS ULP exercise the documented user-space fallback.
    // The local Linux performance gate additionally checks /proc/net/tls_stat.
    (void)offload_active;
}
#endif
