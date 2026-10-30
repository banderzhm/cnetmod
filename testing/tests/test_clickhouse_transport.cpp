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
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.protocol.clickhouse;
namespace cn = cnetmod;
namespace ch = cn::clickhouse;
using bytes = std::vector<unsigned char>;

static void integer(bytes& output, std::uint64_t value)
{
    do
    {
        auto part = static_cast<unsigned char>(value & 127);
        value >>= 7;
        output.push_back(part | (value ? 128 : 0));
    } while (value);
}

static void text(bytes& output, std::string_view value)
{
    integer(output, value.size());
    output.insert(output.end(), value.begin(), value.end());
}

static auto read(cn::io_context& io, cn::socket& peer, std::size_t size) -> cn::task<bytes>
{
    bytes result(size);
    for (std::size_t offset{}; offset < size;)
    {
        auto count = co_await cn::async_read(io, peer, cn::mutable_buffer{result.data() + offset, size - offset});
        if (!count || !*count)
            throw std::runtime_error("fixture peer disconnected");
        offset += *count;
    }
    co_return result;
}

static auto number(cn::io_context& io, cn::socket& peer) -> cn::task<std::uint64_t>
{
    std::uint64_t result{};
    for (unsigned shift{}; shift < 64; shift += 7)
    {
        auto part = co_await read(io, peer, 1);
        result |= std::uint64_t(part[0] & 127) << shift;
        if (!(part[0] & 128))
            co_return result;
    }
    throw std::runtime_error("fixture varint overflow");
}

static auto string(cn::io_context& io, cn::socket& peer) -> cn::task<std::string>
{
    auto size = co_await number(io, peer);
    if (size > 65536)
        throw std::runtime_error("fixture string too large");
    auto value = co_await read(io, peer, size);
    co_return std::string(value.begin(), value.end());
}

static auto send(cn::io_context& io, cn::socket& peer, bytes data, bool fragment = false) -> cn::task<void>
{
    for (std::size_t offset{}; offset < data.size();)
    {
        auto count = co_await cn::async_write(io, peer, cn::const_buffer{data.data() + offset, fragment ? 1 : data.size() - offset});
        if (!count || !*count)
            throw std::runtime_error("fixture write failed");
        offset += *count;
        if (fragment)
            co_await cn::async_sleep(io, std::chrono::milliseconds(1));
    }
}

static auto hello(cn::io_context& io, cn::socket& peer) -> cn::task<void>
{
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{0});
    ASSERT_EQ(co_await string(io, peer), std::string("cnetmod"));
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{2});
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{2});
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{54401});
    ASSERT_EQ(co_await string(io, peer), std::string("default"));
    ASSERT_EQ(co_await string(io, peer), std::string("default"));
    ASSERT_EQ(co_await string(io, peer), std::string(""));
    bytes answer{0};
    text(answer, "ClickHouse");
    integer(answer, 26);
    integer(answer, 9);
    integer(answer, 55000);
    text(answer, "UTC");
    text(answer, "fixture");
    integer(answer, 1);
    co_await send(io, peer, std::move(answer), true);
}

static auto empty_block(cn::io_context& io, cn::socket& peer) -> cn::task<void>
{
    const bytes expected{2, 0, 1, 0, 2, 255, 255, 255, 255, 0, 0, 0};
    ASSERT_TRUE((co_await read(io, peer, expected.size())) == expected);
}

static auto query(cn::io_context& io, cn::socket& peer) -> cn::task<std::string>
{
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{1});
    (void)co_await string(io, peer); // query_id
    ASSERT_TRUE((co_await read(io, peer, 1)) == bytes{1});
    for (int i{}; i < 3; ++i)
        (void)co_await string(io, peer);
    ASSERT_TRUE((co_await read(io, peer, 1)) == bytes{1});
    for (int i{}; i < 3; ++i)
        (void)co_await string(io, peer);
    for (int i{}; i < 3; ++i)
        (void)co_await number(io, peer);
    ASSERT_EQ(co_await string(io, peer), std::string{});
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{0});
    ASSERT_EQ(co_await string(io, peer), std::string{});
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{2});
    ASSERT_EQ(co_await number(io, peer), std::uint64_t{0});
    auto sql = co_await string(io, peer);
    co_await empty_block(io, peer);
    co_return sql;
}

static auto data_packet(unsigned char kind = 1, bool sample = false) -> bytes
{
    bytes packet{kind, 0, 1, 0, 2, 255, 255, 255, 255, 0, 1, static_cast<unsigned char>(sample ? 0 : 2)};
    text(packet, "n");
    text(packet, "UInt16");
    if (!sample)
        packet.insert(packet.end(), {1, 0, 255, 255});
    return packet;
}

using server_work = std::function<cn::task<void>(cn::io_context&, cn::socket&)>;
using client_work = std::function<cn::task<void>(cn::io_context&, ch::client&, ch::connection_options)>;

static void fixture(server_work serve, client_work use)
{
    cn::net_init net;
    auto io = cn::make_io_context();
    auto listener = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cn::endpoint{cn::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto local = listener->local_endpoint();
    ASSERT_TRUE(local.has_value());
    ch::connection_options options;
    options.compression = ch::compression_mode::none;
    options.revision = 54401;
    options.port = local->port();
    options.operation_timeout = std::chrono::seconds(2);
    ch::client client(*io);
    int finished{};
    std::exception_ptr failure;
    auto done = [&]()
    {
        if (++finished == 2)
            io->stop();
    };
    auto server = [&]() -> cn::task<void>
    {
        try
        {
            auto accepted = co_await cn::async_accept(*io, *listener);
            if (!accepted)
                throw std::runtime_error("fixture accept failed");
            co_await serve(*io, *accepted);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        done();
    };
    auto consumer = [&]() -> cn::task<void>
    {
        try
        {
            co_await use(*io, client, options);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        client.close();
        done();
    };
    cn::spawn(*io, server());
    cn::spawn(*io, consumer());
    io->run();
    if (failure)
        std::rethrow_exception(failure);
}

TEST(native_handshake_query_progress_and_connection_reuse)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            ASSERT_EQ(co_await query(io, peer), std::string("SELECT n"));
            auto data = data_packet();
            data.insert(data.end(), {3, 2, 4, 2, 6, 2, 1, 4, 0, 0, 0, 5}); // progress, profile, EOS
            co_await send(io, peer, std::move(data), true);
            ASSERT_TRUE((co_await read(io, peer, 1)) == bytes{4});
            co_await send(io, peer, bytes{4});
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            auto connected = co_await client.connect(options);
            ASSERT_TRUE(connected.has_value());
            ASSERT_EQ(connected->revision, std::uint64_t{55000});
            auto result = co_await client.query("SELECT n");
            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->blocks.size(), std::size_t{1});
            ASSERT_EQ(std::get<std::uint64_t>(result->blocks[0].columns[0].values[1].data), std::uint64_t{65535});
            ASSERT_EQ(result->statistics.rows, std::uint64_t{2});
            ASSERT_EQ(result->profile.blocks, std::uint64_t{1});
            cn::cancel_token cancelled;
            cancelled.cancel();
            auto premature = co_await client.ping(&cancelled);
            ASSERT_FALSE(premature.has_value());
            ASSERT_TRUE(client.is_open());
            ASSERT_TRUE((co_await client.ping()).has_value());
        });
}

TEST(native_insert_checks_schema_and_exact_wire_data)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            ASSERT_EQ(co_await query(io, peer), std::string("INSERT INTO `db`.`ta\\`ble` (`n`) VALUES"));
            co_await send(io, peer, data_packet(1, true));
            auto expected = data_packet();
            expected[0] = 2;
            ASSERT_TRUE((co_await read(io, peer, expected.size())) == expected);
            co_await empty_block(io, peer);
            co_await send(io, peer, bytes{5});
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            auto result = co_await client.insert("db.ta`ble", ch::block{{{"n", "UInt16", {std::uint64_t{1}, std::uint64_t{65535}}}}});
            ASSERT_TRUE(result.has_value());
            ASSERT_TRUE(client.is_open());
        });
}

TEST(native_streaming_backpressure_rejects_overlapping_operation)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            (void)co_await query(io, peer);
            auto data = data_packet();
            auto second = data_packet();
            data.insert(data.end(), second.begin(), second.end());
            data.push_back(5);
            co_await send(io, peer, std::move(data));
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            std::size_t blocks{};
            auto sink = [&](const ch::block& block) -> cn::task<void>
            {
                ++blocks;
                ASSERT_EQ(block.rows(), std::size_t{2});
                auto overlap = co_await client.ping();
                ASSERT_FALSE(overlap.has_value());
                ASSERT_TRUE(overlap.error().kind == ch::error_kind::busy);
                ASSERT_TRUE(client.is_open());
                co_return;
            };
            auto result = co_await client.query_blocks("SELECT n", sink);
            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(blocks, std::size_t{2});
            ASSERT_TRUE(result->blocks.empty());
        });
}

TEST(native_server_exception_is_structured_and_discards_session)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            (void)co_await query(io, peer);
            bytes error{2, 60, 0, 0, 0};
            text(error, "DB::Exception");
            text(error, "missing table");
            text(error, "");
            error.push_back(0);
            co_await send(io, peer, std::move(error));
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            auto result = co_await client.query("SELECT missing");
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error().kind == ch::error_kind::server);
            ASSERT_EQ(result.error().server_code, std::int32_t{60});
            ASSERT_FALSE(client.is_open());
        });
}

TEST(native_deadline_cancels_stalled_read_and_closes_transport)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            (void)co_await query(io, peer);
            co_await cn::async_sleep(io, std::chrono::milliseconds(150));
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            options.operation_timeout = std::chrono::milliseconds(100);
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            auto result = co_await client.query("SELECT stalled");
            ASSERT_FALSE(result.has_value());
            ASSERT_EQ(result.error().cause, std::make_error_code(std::errc::timed_out));
            ASSERT_FALSE(client.is_open());
        });
}

TEST(native_caller_cancellation_discards_inflight_query)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            (void)co_await query(io, peer);
            co_await cn::async_sleep(io, std::chrono::milliseconds(120));
        },
        [](auto& io, auto& client, auto options) -> cn::task<void>
        {
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            cn::cancel_token cancellation;
            auto cancel = [&]() -> cn::task<void>
            {
                co_await cn::async_sleep(io, std::chrono::milliseconds(30));
                cancellation.cancel();
            };
            cn::spawn(io, cancel());
            auto result = co_await client.query("SELECT stalled", "", &cancellation);
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error().kind == ch::error_kind::cancelled);
            ASSERT_FALSE(client.is_open());
        });
}

TEST(native_malformed_packet_closes_session)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            (void)co_await query(io, peer);
            co_await send(io, peer, bytes{99});
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            auto result = co_await client.query("SELECT n");
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error().kind == ch::error_kind::protocol);
            ASSERT_FALSE(client.is_open());
        });
}

TEST(native_sink_exception_closes_session_before_propagation)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            (void)co_await query(io, peer);
            auto data = data_packet();
            data.push_back(5);
            co_await send(io, peer, std::move(data));
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            auto sink = [](const ch::block&) -> cn::task<void>
            {
                throw std::runtime_error("sink failed");
                co_return;
            };
            bool failed{};
            try
            {
                (void)co_await client.query_blocks("SELECT n", sink);
            }
            catch (const std::runtime_error&)
            {
                failed = true;
            }
            ASSERT_TRUE(failed);
            ASSERT_FALSE(client.is_open());
        });
}

TEST(native_buffered_results_enforce_aggregate_limit)
{
    fixture([](auto& io, auto& peer) -> cn::task<void>
        {
            co_await hello(io, peer);
            (void)co_await query(io, peer);
            auto data = data_packet();
            auto second = data_packet();
            data.insert(data.end(), second.begin(), second.end());
            data.push_back(5);
            co_await send(io, peer, std::move(data));
        },
        [](auto&, auto& client, auto options) -> cn::task<void>
        {
            options.bounds.max_rows = 2;
            ASSERT_TRUE((co_await client.connect(options)).has_value());
            auto result = co_await client.query("SELECT n");
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error().kind == ch::error_kind::protocol);
            ASSERT_FALSE(client.is_open());
        });
}
RUN_TESTS()
