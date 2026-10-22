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

// One frontend flight up to and including Sync: the message types in order
// and the statement name of a Parse (empty for the unnamed statement).
struct frontend_flight
{
    std::string types;
    std::string parsed_name;
    bool ok{};
};

static auto read_flight(cn::io_context& io, cn::socket& peer) -> cn::task<frontend_flight>
{
    frontend_flight flight;
    for (;;)
    {
        std::array<std::uint8_t, 5> header{};
        for (std::size_t offset{}; offset < header.size();)
        {
            auto part = co_await cn::async_read(io, peer, cn::mutable_buffer{header.data() + offset, header.size() - offset});
            if (!part || !*part)
                co_return flight;
            offset += *part;
        }
        std::size_t length{};
        for (unsigned index = 1; index < 5; ++index)
            length = (length << 8) | header[index];
        std::vector<std::uint8_t> body(length - 4);
        for (std::size_t offset{}; offset < body.size();)
        {
            auto part = co_await cn::async_read(io, peer, cn::mutable_buffer{body.data() + offset, body.size() - offset});
            if (!part || !*part)
                co_return flight;
            offset += *part;
        }
        const auto type = static_cast<char>(header[0]);
        flight.types.push_back(type);
        if (type == 'P')
            flight.parsed_name.assign(reinterpret_cast<const char*>(body.data()));
        if (type == 'S')
        {
            flight.ok = true;
            co_return flight;
        }
    }
}

TEST(postgresql_parameterized_queries_reuse_named_statements)
{
    for (const std::size_t capacity : {std::size_t{256}, std::size_t{0}})
    {
        cn::net_init network;
        auto io = cn::make_io_context();
        auto listener = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind({cn::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        std::vector<frontend_flight> flights;
        // Replies: success, success, 26000 (statement gone), success.
        const std::array<bool, 4> succeed{true, true, false, true};
        auto server = [&]() -> cn::task<void>
        {
            auto peer = co_await cn::async_accept(*io, *listener);
            ASSERT_TRUE(peer.has_value());
            if (!peer)
                co_return;
            ASSERT_TRUE(co_await read_frontend(*io, *peer, true));
            const std::array<std::uint8_t, 15> authenticated{
                'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};
            ASSERT_TRUE((co_await cn::async_write_all(*io, *peer,
                cn::const_buffer{authenticated.data(), authenticated.size()})).has_value());
            for (const bool ok : succeed)
            {
                auto flight = co_await read_flight(*io, *peer);
                ASSERT_TRUE(flight.ok);
                const bool parsed = flight.types.front() == 'P';
                flights.push_back(std::move(flight));
                std::vector<std::uint8_t> wire;
                if (ok)
                {
                    if (parsed)
                        wire.insert(wire.end(), {'1', 0, 0, 0, 4});
                    wire.insert(wire.end(), {'2', 0, 0, 0, 4,
                        'T', 0, 0, 0, 26, 0, 1, 'v', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23,
                        0, 4, 0xff, 0xff, 0xff, 0xff, 0, 0,
                        'D', 0, 0, 0, 11, 0, 1, 0, 0, 0, 1, '7',
                        'C', 0, 0, 0, 13, 'S', 'E', 'L', 'E', 'C', 'T', ' ', '1', 0});
                }
                else
                {
                    const std::string fields = std::string{"SERROR"} + '\0' + "C26000" + '\0' +
                        "Mprepared statement does not exist" + '\0' + '\0';
                    wire.push_back('E');
                    append_u32(wire, 4 + fields.size());
                    wire.insert(wire.end(), fields.begin(), fields.end());
                }
                wire.insert(wire.end(), {'Z', 0, 0, 0, 5, 'I'});
                ASSERT_TRUE((co_await cn::async_write_all(*io, *peer,
                    cn::const_buffer{wire.data(), wire.size()})).has_value());
            }
            (void)co_await read_frontend(*io, *peer);
        };
        std::vector<bool> outcomes;
        auto exercise = [&]() -> cn::task<void>
        {
            pg::client client{*io};
            pg::connection_options options;
            options.host = "127.0.0.1";
            options.port = endpoint->port();
            options.tls = pg::tls_mode::disable;
            options.statement_cache_capacity = capacity;
            ASSERT_TRUE((co_await client.connect(options)).ok());
            for (std::size_t index{}; index < succeed.size(); ++index)
            {
                auto result = co_await client.execute(pg::with_params(
                    "SELECT v FROM t WHERE id = $1", {pg::param_value::from_int(7)}));
                outcomes.push_back(result.ok() && result.rows.size() == 1U);
            }
            co_await client.terminate();
            io->stop();
        };
        cn::spawn(*io, server());
        cn::spawn(*io, exercise());
        io->run();

        ASSERT_EQ(outcomes.size(), succeed.size());
        for (std::size_t index{}; index < outcomes.size() && index < succeed.size(); ++index)
            ASSERT_EQ(outcomes[index], succeed[index]);
        ASSERT_EQ(flights.size(), 4U);
        if (flights.size() != 4U)
            continue;
        if (capacity == 0U)
        {
            // Disabled: every execution parses the unnamed statement.
            for (const auto& flight : flights)
            {
                ASSERT_EQ(flight.types, std::string{"PBDES"});
                ASSERT_TRUE(flight.parsed_name.empty());
            }
            continue;
        }
        // First: parsed under a name. Second: bound only. Third: still bound
        // (the server reports it missing). Fourth: parsed again, new name.
        ASSERT_EQ(flights[0].types, std::string{"PBDES"});
        ASSERT_FALSE(flights[0].parsed_name.empty());
        ASSERT_EQ(flights[1].types, std::string{"BDES"});
        ASSERT_EQ(flights[2].types, std::string{"BDES"});
        ASSERT_EQ(flights[3].types, std::string{"PBDES"});
        ASSERT_FALSE(flights[3].parsed_name.empty());
        ASSERT_TRUE(flights[3].parsed_name != flights[0].parsed_name);
    }
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
