TEST(mysql_small_and_multipart_packet_writes_preserve_bytes_and_sequence)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    const std::array<std::size_t, 7> lengths{0, 4094, 4095, 4096, 16384, 0xfffffe, 0xffffff};
    unsigned completed{};
    const auto finish = [&] { if (++completed == 2) io->stop(); };
    auto server = [&]() -> cnetmod::task<void>
    {
        auto peer = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(peer.has_value());
        if (!peer) co_return;
        std::vector<std::uint8_t> greeting{10, '8', '.', '0', 0,
            1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0,
            0, 2, 45, 2, 0, 0, 0, 0};
        greeting.resize(greeting.size() + 10, 0);
        greeting.insert(greeting.begin(), {static_cast<std::uint8_t>(greeting.size()), 0, 0, 0});
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
            cnetmod::const_buffer{greeting.data(), greeting.size()})).has_value());
        ASSERT_FALSE((co_await read_mysql_test_packet(*io, *peer)).empty());
        std::array<std::uint8_t, 11> reply{7, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
            cnetmod::const_buffer{reply.data(), reply.size()})).has_value());
        for (const auto length : lengths)
        {
            std::vector<std::uint8_t> command;
            std::uint8_t expected_sequence{};
            for (;;)
            {
                std::uint8_t sequence{};
                auto packet = co_await read_mysql_test_packet(*io, *peer, 0xffffff, &sequence);
                ASSERT_EQ(sequence, expected_sequence++);
                const auto size = packet.size();
                command.insert(command.end(), packet.begin(), packet.end());
                if (size < 0xffffff)
                    break;
            }
            ASSERT_EQ(command.size(), length + 1);
            ASSERT_EQ(command.front(), 3U);
            for (std::size_t index{}; index < length; ++index)
                if (command[index + 1] != static_cast<std::uint8_t>(index % 251))
                {
                    ASSERT_TRUE(false);
                    break;
                }
            reply[3] = expected_sequence;
            ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                cnetmod::const_buffer{reply.data(), reply.size()})).has_value());
        }
        finish();
    };
    auto client = [&]() -> cnetmod::task<void>
    {
        cnetmod::mysql::client connection{*io};
        auto ready = co_await connection.connect({.host = "127.0.0.1", .port = endpoint->port(), .username = "test"});
        ASSERT_TRUE(ready.ok());
        for (const auto length : lengths)
        {
            std::string query(length, '\0');
            for (std::size_t index{}; index < length; ++index)
                query[index] = static_cast<char>(index % 251);
            auto result = co_await connection.query(query);
            ASSERT_TRUE(result.ok());
        }
        connection.close();
        finish();
    };
    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, client());
    io->run();
}
