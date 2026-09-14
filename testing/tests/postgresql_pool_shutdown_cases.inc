TEST(postgresql_pool_close_preserves_claimed_cancellation_resume)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::postgresql::connection_pool_options options;
    options.minimum_connections = 0;
    options.maximum_connections = 1;
    options.connection.tls = cnetmod::postgresql::tls_mode::disable;
    options.connection.maximum_connect_attempts = 1;
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    options.connection.host = "127.0.0.1";
    options.connection.port = listener->local_endpoint()->port();
    cnetmod::postgresql::connection_pool pool{*io, options};
    cnetmod::cancel_token connecting_token;
    auto connecting = pool.acquire(connecting_token);
    connecting.handle().resume();
    // The first connection reserves the only slot while its peer withholds
    // authentication. Race registration against cancellation on another thread.
    for (unsigned iteration = 0; iteration != 256; ++iteration)
    {
        cnetmod::cancel_token racing_token;
        auto racing_waiter = pool.acquire(racing_token);
        std::atomic<bool> begin{};
        {
            std::jthread canceller{[&]
                {
                    begin.wait(false, std::memory_order_acquire);
                    racing_token.cancel();
                }};
            begin.store(true, std::memory_order_release);
            begin.notify_one();
            racing_waiter.handle().resume();
        }
        // All thread joins and polling are outside coroutines. The token and
        // waiter remain alive until the owning executor consumes notification.
        while (!racing_waiter.handle().done())
            io->poll();
        auto rejected = racing_waiter.handle().promise().result();
        ASSERT_FALSE(rejected.has_value());
        if (!rejected)
            ASSERT_EQ(rejected.error(), std::make_error_code(std::errc::operation_canceled));
        ASSERT_EQ(pool.waiter_count(), std::size_t{0});
        ASSERT_FALSE(racing_token.pending_.load(std::memory_order_acquire));
    }
    cnetmod::cancel_token cancellation;
    auto waiting = pool.acquire(cancellation);
    waiting.handle().resume();
    ASSERT_EQ(pool.waiter_count(), std::size_t{1});
    // Cancel from another thread after registration, before the executor drains
    // its notification. The pool itself stays on its owning executor.
    {
        std::jthread canceller{[&]
            {
                cancellation.cancel();
            }};
    }
    auto closing = pool.close();
    closing.handle().resume();
    ASSERT_FALSE(waiting.handle().done());
    ASSERT_FALSE(closing.handle().done());
    ASSERT_EQ(pool.waiter_count(), std::size_t{0});
    auto exercise = [&]() -> cnetmod::task<void>
    {
        connecting_token.cancel();
        while (!connecting.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        ASSERT_FALSE(connecting.handle().promise().result().has_value());
        while (!waiting.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(waiting.handle().done());
        ASSERT_FALSE(waiting.handle().promise().result().has_value());
        while (!closing.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        closing.handle().promise().result();
        io->stop();
    };
    auto exercise_task = exercise();
    io->post(exercise_task.handle());
    io->run();
    ASSERT_TRUE(exercise_task.handle().done());
    exercise_task.handle().promise().result();
}

TEST(postgresql_pool_close_joins_background_reconnect)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::postgresql::connection_pool_options options;
    options.connection.host = "127.0.0.1";
    options.connection.port = endpoint->port();
    options.connection.tls = cnetmod::postgresql::tls_mode::disable;
    options.connection.maximum_connect_attempts = 1;
    options.minimum_connections = 0;
    options.maximum_connections = 1;
    cnetmod::postgresql::connection_pool pool{*io, options};
    ASSERT_FALSE(static_cast<bool>(pool.background_error()));
    bool reconnect_waiting = false;
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        for (const bool reconnect : {false, true})
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection.has_value());
            if (!connection)
                break;
            std::array<std::byte, 4096> bytes{};
            auto startup = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(startup.has_value());
            if (!reconnect)
            {
                const std::array<std::uint8_t, 15> ready{
                    'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};
                ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection,
                                 cnetmod::const_buffer{ready.data(), ready.size()}))
                        .has_value());
                std::size_t received = 0;
                while (received != 5)
                {
                    auto part = co_await cnetmod::async_read(*io, *connection,
                        cnetmod::mutable_buffer{bytes.data() + received, 5 - received});
                    ASSERT_TRUE(part && *part != 0);
                    if (!part || *part == 0)
                        break;
                    received += *part;
                }
                ASSERT_EQ(received, std::size_t{5});
                ASSERT_TRUE(bytes[0] == std::byte{'X'});
            }
            else
            {
                reconnect_waiting = true;
                auto eof = co_await cnetmod::async_read(*io, *connection,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                ASSERT_TRUE(!eof || *eof == 0);
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token first_token;
        auto first = co_await pool.acquire(first_token);
        ASSERT_TRUE(first.has_value());
        cnetmod::cancel_token cancelled_shutdown;
        cancelled_shutdown.cancel();
        auto preserved = co_await first->get().terminate(cancelled_shutdown);
        ASSERT_FALSE(preserved.has_value());
        if (!preserved)
            ASSERT_EQ(preserved.error(), std::make_error_code(std::errc::operation_canceled));
        ASSERT_TRUE(first->get().is_open());
        cnetmod::cancel_token shutdown;
        ASSERT_TRUE((co_await first->get().terminate(shutdown)).has_value());
        ASSERT_FALSE(first->get().is_open());
        cnetmod::cancel_token waiting_token;
        auto waiting = pool.acquire(waiting_token);
        io->post(waiting.handle());
        while (pool.waiter_count() == 0)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        first->discard();
        *first = cnetmod::postgresql::pooled_connection{};
        while (!reconnect_waiting)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        const auto started = std::chrono::steady_clock::now();
        std::vector<unsigned> close_order;
        auto close = [&](unsigned index) -> cnetmod::task<void>
        {
            co_await pool.close();
            ASSERT_EQ(pool.size(), std::size_t{0});
            ASSERT_EQ(pool.waiter_count(), std::size_t{0});
            close_order.push_back(index);
        };
        auto first_close = close(1);
        auto second_close = close(2);
        io->post(first_close.handle());
        io->post(second_close.handle());
        while (!first_close.handle().done() || !second_close.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        first_close.handle().promise().result();
        second_close.handle().promise().result();
        ASSERT_EQ(close_order.size(), std::size_t{2});
        std::ranges::sort(close_order);
        ASSERT_EQ(close_order[0], 1U);
        ASSERT_EQ(close_order[1], 2U);
        co_await pool.close();
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
        while (!waiting.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        auto rejected = waiting.handle().promise().result();
        ASSERT_FALSE(rejected.has_value());
        ASSERT_EQ(pool.size(), std::size_t{0});
        ASSERT_EQ(pool.waiter_count(), std::size_t{0});
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
