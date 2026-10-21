#include "test_framework.hpp"

import std;
import cnetmod.core.net_init;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.postgresql;

namespace cn = cnetmod;
namespace pg = cn::postgresql;

static auto read_frontend(cn::io_context& io, cn::socket& peer, bool startup = false)
    -> cn::task<bool>
{
    std::array<std::uint8_t, 5> header{};
    const auto size = startup ? 4U : 5U;
    for (std::size_t offset{}; offset < size;)
    {
        auto part = co_await cn::async_read(io, peer, cn::mutable_buffer{header.data() + offset, size - offset});
        if (!part || !*part)
            co_return false;
        offset += *part;
    }
    const auto base = startup ? 0U : 1U;
    std::size_t length{};
    for (unsigned index = base; index < base + 4; ++index)
        length = (length << 8) | header[index];
    if (length < 4 || length > 65536)
        co_return false;
    std::vector<std::uint8_t> body(length - 4);
    for (std::size_t offset{}; offset < body.size();)
    {
        auto part = co_await cn::async_read(io, peer, cn::mutable_buffer{body.data() + offset, body.size() - offset});
        if (!part || !*part)
            co_return false;
        offset += *part;
    }
    co_return true;
}

static void append_u32(std::vector<std::uint8_t>& wire, std::size_t value)
{
    for (int shift : {24, 16, 8, 0})
        wire.push_back(static_cast<std::uint8_t>(value >> shift));
}

TEST(postgresql_coalesced_fragmented_messages_and_pool_accounting)
{
    for (const bool fragmented : {false, true})
    {
        cn::net_init network;
        auto io = cn::make_io_context();
        auto listener = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind({cn::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        const std::array<std::uint8_t, 15> authenticated{
            'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};
        unsigned finished{};
        const auto finish = [&] { if (++finished == 2) io->stop(); };
        auto server = [&]() -> cn::task<void>
        {
            std::vector<cn::socket> peers;
            for (unsigned index{}; index < 8; ++index)
            {
                auto peer = co_await cn::async_accept(*io, *listener);
                ASSERT_TRUE(peer.has_value());
                if (!peer) co_return;
                ASSERT_TRUE(co_await read_frontend(*io, *peer, true));
                ASSERT_TRUE((co_await cn::async_write_all(*io, *peer,
                    cn::const_buffer{authenticated.data(), authenticated.size()})).has_value());
                peers.push_back(std::move(*peer));
            }
            auto& peer = peers.front();
            for (unsigned query{}; query < 3; ++query)
            {
                ASSERT_TRUE(co_await read_frontend(*io, peer));
                std::vector<std::uint8_t> wire{'T', 0, 0, 0, 26, 0, 1, 'v', 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 25, 0xff, 0xff,
                    0xff, 0xff, 0xff, 0xff, 0, 0};
                const std::string text(13000 + query, static_cast<char>('a' + query));
                wire.push_back('D');
                append_u32(wire, 10 + text.size());
                wire.insert(wire.end(), {0, 1});
                append_u32(wire, text.size());
                wire.insert(wire.end(), text.begin(), text.end());
                const std::array<std::uint8_t, 20> tail{
                    'C', 0, 0, 0, 13, 'S', 'E', 'L', 'E', 'C', 'T', ' ', '1', 0,
                    'Z', 0, 0, 0, 5, 'I'};
                wire.insert(wire.end(), tail.begin(), tail.end());
                const auto first = fragmented ? 3U : wire.size();
                ASSERT_TRUE((co_await cn::async_write_all(*io, peer,
                    cn::const_buffer{wire.data(), first})).has_value());
                if (fragmented)
                {
                    co_await cn::async_sleep(*io, std::chrono::milliseconds(1));
                    ASSERT_TRUE((co_await cn::async_write_all(*io, peer,
                        cn::const_buffer{wire.data() + first, wire.size() - first})).has_value());
                }
            }
            for (auto& connection : peers)
                ASSERT_TRUE(co_await read_frontend(*io, connection));
            finish();
        };
        auto exercise = [&]() -> cn::task<void>
        {
            pg::connection_pool_options options;
            options.connection.host = "127.0.0.1";
            options.connection.port = endpoint->port();
            options.connection.tls = pg::tls_mode::disable;
            options.minimum_connections = options.maximum_connections = 8;
            pg::connection_pool pool{*io, options};
            ASSERT_TRUE((co_await pool.warm_up()).ok());
            for (unsigned query{}; query < 3; ++query)
            {
                auto lease = co_await pool.acquire();
                ASSERT_TRUE(lease.has_value());
                ASSERT_EQ(pool.idle_count(), 7U);
                ASSERT_EQ(pool.checked_out_count(), 1U);
                auto result = co_await (*lease)->query("SELECT v");
                ASSERT_TRUE(result.ok());
                ASSERT_EQ(result.rows.size(), 1U);
                if (!result.rows.empty())
                    ASSERT_EQ(result.rows[0][0].as_string(), std::string(13000 + query, static_cast<char>('a' + query)));
            }
            ASSERT_EQ(pool.idle_count(), 8U);
            ASSERT_EQ(pool.checked_out_count(), 0U);
            co_await pool.close();
            ASSERT_EQ(pool.size(), 0U);
            ASSERT_EQ(pool.idle_count(), 0U);
            finish();
        };
        cn::spawn(*io, server());
        cn::spawn(*io, exercise());
        io->run();
    }
}

RUN_TESTS()
