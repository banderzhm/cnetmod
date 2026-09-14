static void verify_postgresql_service_recovery_and_stop(bool concurrent_close)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
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
    options.minimum_connections = 1;
    options.maximum_connections = 1;
    cnetmod::application::postgresql_service service{*io, options, "test",
        cnetmod::application::service_requirement::required, {}};
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    unsigned finished = 0;
    const auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        for (const bool recover : {false, true})
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection.has_value());
            if (!connection)
                break;
            std::array<std::byte, 4096> bytes{};
            auto startup = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(startup.has_value());
            const std::array<std::uint8_t, 15> ready{
                'R', 0, 0, 0, 8, 0, 0, 0, 0,
                'Z', 0, 0, 0, 5, 'I'};
            auto sent = co_await cnetmod::async_write_all(*io, *connection,
                cnetmod::const_buffer{ready.data(), ready.size()});
            ASSERT_TRUE(sent.has_value());
            auto query = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(query.has_value());
            if (query)
                ASSERT_TRUE((std::string_view{reinterpret_cast<const char*>(bytes.data()), *query}.find("SELECT 1") != std::string_view::npos));
            if (recover)
            {
                auto response = co_await cnetmod::async_write_all(*io, *connection,
                    cnetmod::const_buffer{ready.data() + 9, 6});
                ASSERT_TRUE(response.has_value());
                for (;;)
                {
                    auto received = co_await cnetmod::async_read(*io, *connection,
                        cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                    if (!received || *received == 0)
                        break;
                }
            }
            else
            {
                auto eof = co_await cnetmod::async_read(*io, *connection,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                ASSERT_TRUE(!eof || *eof == 0);
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token startup_token;
        cnetmod::application::service_context startup{*io, telemetry, supervisor, startup_token, {}};
        ASSERT_TRUE((co_await service.start(startup)).has_value());
        cnetmod::cancel_token timeout_token;
        cnetmod::application::service_context timed{*io, telemetry, supervisor, timeout_token,
            cnetmod::deadline::after(std::chrono::milliseconds{50})};
        auto failed = co_await service.probe(timed);
        ASSERT_TRUE(failed.status == cnetmod::application::service_health::down);
        ASSERT_EQ(failed.error, std::make_error_code(std::errc::timed_out));
        ASSERT_EQ(service.pool().checked_out_count(), std::size_t{0});
        auto recovered = co_await service.probe(startup);
        ASSERT_TRUE(recovered.status == cnetmod::application::service_health::up);
        ASSERT_FALSE(static_cast<bool>(recovered.error));
        auto held = co_await service.pool().acquire(startup_token);
        ASSERT_TRUE(held.has_value());
        std::optional<cnetmod::task<void>> first_close;
        if (concurrent_close)
        {
            first_close.emplace(service.pool().close());
            first_close->handle().resume();
            ASSERT_FALSE(first_close->handle().done());
        }
        cnetmod::cancel_token stopping_token;
        cnetmod::application::service_context stopping{*io, telemetry, supervisor, stopping_token,
            cnetmod::deadline::after(std::chrono::milliseconds{30})};
        const auto before_stop = std::chrono::steady_clock::now();
        auto interrupted = co_await service.stop(stopping);
        ASSERT_FALSE(interrupted.has_value());
        if (!interrupted)
            ASSERT_EQ(interrupted.error(), std::make_error_code(std::errc::timed_out));
        ASSERT_TRUE(std::chrono::steady_clock::now() - before_stop < std::chrono::seconds{1});
        ASSERT_EQ(service.pool().checked_out_count(), std::size_t{1});
        ASSERT_TRUE(held->get().is_open());
        if (first_close)
            ASSERT_FALSE(first_close->handle().done());
        *held = cnetmod::postgresql::pooled_connection{};
        if (first_close)
        {
            while (!first_close->handle().done())
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
            first_close->handle().promise().result();
            ASSERT_EQ(service.pool().size(), std::size_t{0});
        }
        ASSERT_TRUE((co_await service.stop(startup)).has_value());
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

TEST(postgresql_service_probe_times_out_discards_and_recovers)
{
    verify_postgresql_service_recovery_and_stop(false);
}

TEST(postgresql_service_stop_deadline_does_not_cancel_another_close_owner)
{
    verify_postgresql_service_recovery_and_stop(true);
}

TEST(postgresql_query_cancellation_discards_waiting_session)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::postgresql::client client{*io};
    cnetmod::postgresql::connection_options options;
    options.host = "127.0.0.1";
    options.port = endpoint->port();
    options.tls = cnetmod::postgresql::tls_mode::disable;
    options.maximum_connect_attempts = 1;
    cnetmod::cancel_token cancellation;
    unsigned finished = 0;
    const auto finish = [&]
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
            std::array<std::byte, 4096> bytes{};
            auto startup = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(startup.has_value());
            const std::array<std::uint8_t, 15> ready{
                'R', 0, 0, 0, 8, 0, 0, 0, 0,
                'Z', 0, 0, 0, 5, 'I'};
            auto sent = co_await cnetmod::async_write_all(*io, *connection,
                cnetmod::const_buffer{ready.data(), ready.size()});
            ASSERT_TRUE(sent.has_value());
            auto query = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(query.has_value());
            if (query && *query > 0)
                ASSERT_TRUE(bytes[0] == std::byte{'Q'});
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{5});
            cnetmod::cancel_token competing;
            auto overlap = co_await client.query("SELECT 2", competing);
            ASSERT_TRUE(overlap.is_err());
            cancellation.cancel();
            auto eof = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(!eof || *eof == 0);
            connection->close();
            connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection.has_value());
            if (connection)
            {
                auto second_startup = co_await cnetmod::async_read(*io, *connection,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                ASSERT_TRUE(second_startup.has_value());
                auto authenticated = co_await cnetmod::async_write_all(*io, *connection,
                    cnetmod::const_buffer{ready.data(), ready.size()});
                ASSERT_TRUE(authenticated.has_value());
                auto second_query = co_await cnetmod::async_read(*io, *connection,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                ASSERT_TRUE(second_query.has_value());
                if (second_query)
                {
                    const std::string_view command{reinterpret_cast<const char*>(bytes.data()), *second_query};
                    ASSERT_TRUE(command.find("SELECT 4") != std::string_view::npos);
                }
                const std::array<std::uint8_t, 20> response{
                    'C', 0, 0, 0, 13, 'S', 'E', 'L', 'E', 'C', 'T', ' ', '0', 0,
                    'Z', 0, 0, 0, 5, 'I'};
                auto completed = co_await cnetmod::async_write_all(*io, *connection,
                    cnetmod::const_buffer{response.data(), response.size()});
                ASSERT_TRUE(completed.has_value());
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto connected = co_await client.connect(options);
        ASSERT_FALSE(connected.is_err());
        cnetmod::cancel_token already_cancelled;
        already_cancelled.cancel();
        auto rejected = co_await client.query("SELECT 0", already_cancelled);
        ASSERT_TRUE(rejected.is_err());
        ASSERT_TRUE(client.is_open());
        const auto started = std::chrono::steady_clock::now();
        auto result = co_await client.query("SELECT 1", cancellation);
        ASSERT_TRUE(result.is_err());
        ASSERT_FALSE(client.is_open());
        ASSERT_TRUE(static_cast<bool>(client.last_error()));
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
        auto after = co_await client.query("SELECT 3");
        ASSERT_TRUE(after.is_err());
        // Leave the previous token cancelled: reconnect and raw queries must
        // not inherit a stale binding from the discarded operation.
        ASSERT_TRUE(cancellation.is_cancelled());
        auto reconnected = co_await client.reconnect();
        ASSERT_FALSE(reconnected.is_err());
        ASSERT_TRUE(client.is_open());
        ASSERT_FALSE(static_cast<bool>(client.last_error()));
        auto recovered = co_await client.query("SELECT 4");
        ASSERT_FALSE(recovered.is_err());
        ASSERT_EQ(recovered.info, std::string{"SELECT 0"});
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

TEST(postgresql_reconnect_does_not_close_an_active_handshake)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::postgresql::client client{*io};
    cnetmod::postgresql::connection_options options;
    options.host = "127.0.0.1";
    options.port = endpoint->port();
    options.tls = cnetmod::postgresql::tls_mode::disable;
    options.maximum_connect_attempts = 1;
    unsigned finished = 0;
    const auto finish = [&]
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
            std::array<std::byte, 4096> bytes{};
            auto received = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(received.has_value());
            if (received)
                ASSERT_TRUE(*received > 0);
            // The original connect owns the client until ReadyForQuery arrives.
            auto overlap = co_await client.reconnect();
            ASSERT_TRUE(overlap.is_err());
            ASSERT_FALSE(static_cast<bool>(client.last_error()));
            const std::array<std::uint8_t, 15> ready{
                'R', 0, 0, 0, 8, 0, 0, 0, 0,
                'Z', 0, 0, 0, 5, 'I'};
            auto sent = co_await cnetmod::async_write_all(*io, *connection,
                cnetmod::const_buffer{ready.data(), ready.size()});
            ASSERT_TRUE(sent.has_value());
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto result = co_await client.connect(options);
        ASSERT_FALSE(result.is_err());
        ASSERT_TRUE(client.is_open());
        ASSERT_FALSE(static_cast<bool>(client.last_error()));
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

static void verify_postgresql_authentication_failure(bool cancel_authentication, int pool_operation = 0)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::postgresql::client client{*io};
    cnetmod::postgresql::connection_options options;
    options.host = "127.0.0.1";
    options.port = endpoint->port();
    options.tls = cnetmod::postgresql::tls_mode::disable;
    options.maximum_connect_attempts = 1;
    cnetmod::cancel_token cancellation;
    unsigned finished = 0;
    const auto finish = [&]
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
            std::array<std::byte, 4096> bytes{};
            auto received = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            ASSERT_TRUE(received.has_value());
            if (received)
                ASSERT_TRUE(*received > 0);
            if (cancel_authentication)
            {
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{5});
                cancellation.cancel();
                auto eof = co_await cnetmod::async_read(*io, *connection,
                    cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                ASSERT_TRUE(!eof || *eof == 0);
            }
            else
            {
                // Drop the transport before any AuthenticationRequest is sent.
                connection->close();
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        const auto started = std::chrono::steady_clock::now();
        if (pool_operation != 0)
        {
            cnetmod::postgresql::connection_pool_options pool_options;
            pool_options.connection = options;
            pool_options.minimum_connections = pool_operation == 2 ? 1 : 0;
            pool_options.maximum_connections = 1;
            cnetmod::postgresql::connection_pool pool{*io, pool_options};
            cnetmod::cancel_token rejected;
            rejected.cancel();
            auto admission = co_await pool.acquire(rejected);
            ASSERT_FALSE(admission.has_value());
            ASSERT_EQ(pool.size(), std::size_t{0});
            std::error_code error;
            if (pool_operation == 2)
            {
                auto warmed = co_await pool.warm_up(cancellation);
                ASSERT_FALSE(warmed.has_value());
                error = warmed.error();
            }
            else
            {
                auto acquired = co_await pool.acquire(cancellation);
                ASSERT_FALSE(acquired.has_value());
                error = acquired.error();
            }
            ASSERT_TRUE(static_cast<bool>(error));
            ASSERT_FALSE(error == std::errc::connection_refused);
            ASSERT_EQ(pool.checked_out_count(), std::size_t{0});
            ASSERT_EQ(pool.waiter_count(), std::size_t{0});
            co_await pool.close();
            ASSERT_EQ(pool.size(), std::size_t{0});
            ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
            finish();
            co_return;
        }
        auto result = cancel_authentication ? co_await client.connect(options, cancellation)
                                            : co_await client.connect(options);
        ASSERT_TRUE(result.is_err());
        ASSERT_FALSE(client.is_open());
        ASSERT_TRUE(static_cast<bool>(client.last_error()));
        ASSERT_EQ(client.backend_process_id(), std::uint32_t{0});
        ASSERT_EQ(client.backend_secret_key(), std::uint32_t{0});
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
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

TEST(postgresql_authentication_disconnect_preserves_transport_error)
{
    verify_postgresql_authentication_failure(false);
}

TEST(postgresql_authentication_cancellation_closes_waiting_transport)
{
    verify_postgresql_authentication_failure(true);
}

TEST(postgresql_pool_acquire_cancels_authentication_and_releases_reserved_slot)
{
    verify_postgresql_authentication_failure(true, 1);
}

TEST(postgresql_pool_warmup_cancels_authentication_and_releases_reserved_slot)
{
    verify_postgresql_authentication_failure(true, 2);
}
