namespace {

auto read_mysql_test_packet(cnetmod::io_context& io, cnetmod::socket& peer)
    -> cnetmod::task<std::vector<std::uint8_t>>
{
    std::array<std::uint8_t, 4> header{};
    std::size_t offset = 0;
    while (offset < header.size())
    {
        auto read = co_await cnetmod::async_read(io, peer,
            cnetmod::mutable_buffer{header.data() + offset, header.size() - offset});
        if (!read || *read == 0)
            co_return std::vector<std::uint8_t>{};
        offset += *read;
    }
    const std::size_t length = header[0] | (std::size_t{header[1]} << 8) |
        (std::size_t{header[2]} << 16);
    ASSERT_TRUE(length <= 4096);
    if (length > 4096)
        co_return std::vector<std::uint8_t>{};
    std::vector<std::uint8_t> payload(length);
    offset = 0;
    while (offset < length)
    {
        auto read = co_await cnetmod::async_read(io, peer,
            cnetmod::mutable_buffer{payload.data() + offset, length - offset});
        if (!read || *read == 0)
            co_return std::vector<std::uint8_t>{};
        offset += *read;
    }
    co_return payload;
}

void verify_mysql_reset_interruption(bool stop_during_reset,
    std::span<const std::uint8_t> reset_reply = {})
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener)
        return;
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    cnetmod::mysql::pool_params options;
    options.port = endpoint->port();
    options.ssl = cnetmod::mysql::ssl_mode::disable;
    options.initial_size = options.max_size = 1;
    options.ping_timeout = stop_during_reset ? std::chrono::seconds{10} : std::chrono::milliseconds{30};
    cnetmod::mysql::connection_pool pool{*io, options};
    unsigned completed = 0;
    bool reset_received = false;
    bool peer_closed = false;
    bool replacement_authenticated = false;
    bool replacement_ping_received = false;
    cnetmod::mysql::client* returned_client = nullptr;
    auto finish = [&]
    {
        if (++completed == 3)
            io->stop();
    };
    auto server = [&]() -> cnetmod::task<void>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        if (!accepted)
        {
            pool.request_stop();
            finish();
            co_return;
        }
        // Protocol 4.1 greeting; the fixture accepts an empty-password login.
        std::vector<std::uint8_t> greeting{10, '8', '.', '0', 0,
            1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0,
            0, 2, 45, 2, 0, 0, 0, 0};
        greeting.resize(greeting.size() + 10, 0);
        greeting.insert(greeting.begin(), {static_cast<std::uint8_t>(greeting.size()), 0, 0, 0});
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *accepted,
                         cnetmod::const_buffer{greeting.data(), greeting.size()}))
                .has_value());
        auto login = co_await read_mysql_test_packet(*io, *accepted);
        ASSERT_FALSE(login.empty());
        const std::array<std::uint8_t, 11> authenticated{7, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *accepted,
                         cnetmod::const_buffer{authenticated.data(), authenticated.size()}))
                .has_value());
        const auto command = co_await read_mysql_test_packet(*io, *accepted);
        reset_received = command.size() == 1 && command.front() == 0x1f;
        ASSERT_TRUE(reset_received);
        ASSERT_EQ(pool.idle_count(), 0U);
        if (!reset_reply.empty())
        {
            std::vector<std::uint8_t> reply{static_cast<std::uint8_t>(reset_reply.size()), 0, 0, 1};
            reply.insert(reply.end(), reset_reply.begin(), reset_reply.end());
            ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *accepted,
                             cnetmod::const_buffer{reply.data(), reply.size()}))
                    .has_value());
        }
        if (stop_during_reset)
            pool.request_stop();
        std::uint8_t ignored{};
        const auto closed = co_await cnetmod::async_read(*io, *accepted,
            cnetmod::mutable_buffer{&ignored, 1});
        peer_closed = !closed || *closed == 0;
        if (!stop_during_reset)
        {
            auto replacement = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(replacement.has_value());
            if (replacement)
            {
                ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *replacement,
                                 cnetmod::const_buffer{greeting.data(), greeting.size()}))
                        .has_value());
                auto next_login = co_await read_mysql_test_packet(*io, *replacement);
                replacement_authenticated = !next_login.empty();
                ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *replacement,
                                 cnetmod::const_buffer{authenticated.data(), authenticated.size()}))
                        .has_value());
                auto ping = co_await read_mysql_test_packet(*io, *replacement);
                replacement_ping_received = ping.size() == 1 && ping.front() == 0x0e;
                const std::array<std::uint8_t, 11> pong{7, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
                ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *replacement,
                                 cnetmod::const_buffer{pong.data(), pong.size()}))
                        .has_value());
            }
        }
        else
            pool.request_stop();
        finish();
    };
    auto workload = [&]() -> cnetmod::task<void>
    {
        {
            auto lease = co_await pool.async_get_connection();
            ASSERT_TRUE(lease.has_value());
            ASSERT_EQ(pool.checked_out_count(), lease ? 1U : 0U);
        }
        ASSERT_EQ(pool.checked_out_count(), 0U);
        if (!stop_during_reset)
        {
            auto replacement = co_await pool.async_get_connection(
                cnetmod::deadline::after(std::chrono::seconds{2}));
            ASSERT_TRUE(replacement.has_value());
            if (replacement)
            {
                ASSERT_EQ(pool.checked_out_count(), 1U);
                auto pong = co_await (*replacement)->ping();
                ASSERT_FALSE(pong.is_err());
                returned_client = &replacement->get();
                replacement->return_without_reset();
                ASSERT_EQ(pool.checked_out_count(), 0U);
            }
            ASSERT_EQ(pool.size(), 1U);
            pool.request_stop();
        }
        finish();
    };
    auto maintenance = [&]() -> cnetmod::task<void>
    {
        co_await pool.async_run();
        finish();
    };
    auto server_task = server();
    auto maintenance_task = maintenance();
    auto workload_task = workload();
    io->post(server_task.handle());
    io->post(maintenance_task.handle());
    io->post(workload_task.handle());
    io->run();
    for (auto* operation : {&server_task, &maintenance_task, &workload_task})
    {
        ASSERT_TRUE(operation->handle().done());
        operation->handle().promise().result();
    }
    ASSERT_TRUE(reset_received);
    ASSERT_TRUE(peer_closed);
    if (!stop_during_reset)
    {
        ASSERT_TRUE(replacement_authenticated);
        ASSERT_TRUE(replacement_ping_received);
        ASSERT_TRUE(returned_client != nullptr);
        ASSERT_FALSE(returned_client->is_open());
    }
    ASSERT_EQ(pool.waiter_count(), 0U);
}

} // namespace

TEST(mysql_pool_reset_timeout_reconnects_and_serves_waiting_borrower)
{
    verify_mysql_reset_interruption(false);
}

TEST(mysql_pool_stop_cancels_pending_session_reset)
{
    verify_mysql_reset_interruption(true);
}

TEST(mysql_pool_rejects_malformed_reset_responses_before_reuse)
{
    for (const auto& reply : std::vector<std::vector<std::uint8_t>>{
             {0x7f}, {0x00}, {0xff}, {0x00, 0xfc, 0x00},
             {0x00, 0x00, 0x00, 0x02}})
        verify_mysql_reset_interruption(false, reply);
}

TEST(mysql_pool_reconnects_after_server_rejects_session_reset)
{
    const std::array<std::uint8_t, 10> rejected{0xff, 0x15, 0x04, '#', 'H', 'Y', '0', '0', '0', 'x'};
    verify_mysql_reset_interruption(false, rejected);
}
