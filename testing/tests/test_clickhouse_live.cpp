#include "test_framework.hpp"
import std;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.protocol.clickhouse;
namespace cn = cnetmod;
namespace ch = cn::clickhouse;

template <typename T> static auto checked(std::expected<T, ch::error> result) -> T
{
    if (!result)
        throw std::runtime_error(std::to_string(result.error().server_code) + ": " + result.error().message);
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}

TEST(native_live_roundtrip)
{
    cn::net_init net;
    auto io = cn::make_io_context();
    ch::client client(*io);
    ch::connection_options options;
    options.port = static_cast<std::uint16_t>(std::stoul(std::getenv("CNETMOD_CLICKHOUSE_TEST_PORT")));
    if (const auto* mode = std::getenv("CNETMOD_CLICKHOUSE_COMPRESSION"))
    {
        const std::string_view name{mode};
        options.compression = name == "none" ? ch::compression_mode::none : name == "checksum" ? ch::compression_mode::checksum : name == "zstd" ? ch::compression_mode::zstd : ch::compression_mode::lz4;
    }
    std::exception_ptr failure;
    auto work = [&]() -> cn::task<void>
    {
        const auto table = "cnetmod_native_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        bool created{};
        try
        {
            checked(co_await client.connect(options));
            checked(co_await client.ping());
            auto result = checked(co_await client.query("SELECT toUInt64(42) AS n, '你好' AS text, toLowCardinality('compact') AS lc, CAST(NULL, 'Nullable(Int32)') AS missing"));
            ASSERT_EQ(result.blocks.size(), std::size_t{2}); // Server sends a schema block then a row block.
            const auto& row = result.blocks.back();
            ASSERT_EQ(std::get<std::uint64_t>(row.columns[0].values[0].data), std::uint64_t{42});
            ASSERT_EQ(std::get<std::string>(row.columns[1].values[0].data), std::string("你好"));
            ASSERT_EQ(row.columns[2].type, std::string("LowCardinality(String)"));
            ASSERT_EQ(std::get<std::string>(row.columns[2].values[0].data), std::string("compact"));
            ASSERT_TRUE(std::holds_alternative<std::monostate>(row.columns[3].values[0].data));
            ch::query_options request;
            request.settings.push_back({"network_compression_method", "ZSTD"});
            request.parameters.push_back({"message", "quote'\\ and 中文"});
            request.parameters.push_back({"number", "18446744073709551615"});
            auto parameterized = checked(co_await client.query("SELECT {message:String}, {number:UInt64}", request));
            ASSERT_EQ(std::get<std::string>(parameterized.blocks.back().columns[0].values[0].data), request.parameters[0].value);
            ASSERT_EQ(std::get<std::uint64_t>(parameterized.blocks.back().columns[1].values[0].data), std::numeric_limits<std::uint64_t>::max());
            ch::query_options external;
            external.external_tables.push_back({"source",ch::block{{{"n","UInt64",{std::uint64_t{7},std::uint64_t{8}}}}}});
            auto external_rows=checked(co_await client.query("SELECT sum(n) FROM source",external));
            ASSERT_EQ(std::get<std::uint64_t>(external_rows.blocks.back().columns[0].values[0].data),std::uint64_t{15});
            auto large = checked(co_await client.query("SELECT repeat('abcdefgh', 20000)"));
            ASSERT_EQ(std::get<std::string>(large.blocks.back().columns[0].values[0].data).size(), std::size_t{160000});
            checked(co_await client.query("CREATE TABLE " + table + " (n UInt16, text String, missing Nullable(Int32), items Array(UInt8), amount Decimal(18, 4)) ENGINE=Memory"));
            created = true;
            using seq = ch::value::sequence;
            ch::block data{{{"n", "UInt16", {std::uint64_t{1}, std::uint64_t{65535}}},
                {"text", "String", {std::string(160000, 'L'), std::string("a\0b", 3)}},
                {"missing", "Nullable(Int32)", {ch::value{}, std::int64_t{-7}}},
                {"items", "Array(UInt8)", {seq{std::uint64_t{1}, std::uint64_t{2}}, seq{}}},
                {"amount", "Decimal(18, 4)", {std::int64_t{12345}, std::int64_t{-12345}}}}};
            checked(co_await client.insert(table, data));
            auto rows = checked(co_await client.query("SELECT * FROM " + table + " ORDER BY n"));
            ASSERT_TRUE(rows.blocks.back() == data);
            std::size_t received{};
            auto sink = [&](const ch::block& block) -> cn::task<void>
            {
                received += block.rows();
                co_return;
            };
            auto streamed = checked(co_await client.query_blocks("SELECT number FROM numbers(10) SETTINGS max_block_size=3", sink));
            ASSERT_TRUE(streamed.blocks.empty());
            ASSERT_EQ(received, std::size_t{10});
            auto bad = co_await client.query("SELECT unknown_column FROM " + table);
            ASSERT_FALSE(bad.has_value());
            ASSERT_TRUE(bad.error().kind == ch::error_kind::server);
            ASSERT_FALSE(client.is_open());
            checked(co_await client.connect(options));
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (created && client.is_open())
            (void)co_await client.query("DROP TABLE IF EXISTS " + table);
        client.close();
        io->stop();
    };
    cn::spawn(*io, work());
    io->run();
    if (failure)
        std::rethrow_exception(failure);
}


TEST(native_live_cancel_packet_interrupts_server_query)
{
    cn::net_init net;auto io=cn::make_io_context();ch::client client(*io);
    ch::connection_options options;options.port=static_cast<std::uint16_t>(std::stoul(std::getenv("CNETMOD_CLICKHOUSE_TEST_PORT")));
    std::exception_ptr failure;bool sent{};
    auto cancel=[&]()->cn::task<void>
    {
        co_await cn::async_sleep(*io,std::chrono::milliseconds(50));
        auto result=co_await client.cancel_current_query();
        ASSERT_TRUE(result.has_value());sent=true;
    };
    auto work=[&]()->cn::task<void>
    {
        try
        {
            checked(co_await client.connect(options));
            cn::spawn(*io,cancel());
            auto result=co_await client.query("SELECT number,sleepEachRow(0.01) FROM numbers(1000) SETTINGS max_block_size=1");
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error().kind==ch::error_kind::cancelled);
            ASSERT_TRUE(sent);
            ASSERT_FALSE(client.is_open());
        }
        catch(...){failure=std::current_exception();}
        client.close();io->stop();
    };
    cn::spawn(*io,work());io->run();if(failure)std::rethrow_exception(failure);
}
#ifdef CNETMOD_HAS_SSL
TEST(native_live_tls_verifies_ca_and_hostname)
{
    const char* tls_port = std::getenv("CNETMOD_CLICKHOUSE_TLS_PORT");
    const char* ca_file = std::getenv("CNETMOD_CLICKHOUSE_CA_FILE");
    if (!tls_port || !ca_file)
        return;
    cn::net_init net;
    auto io = cn::make_io_context();
    ch::client client(*io);
    ch::connection_options options;
    options.port = static_cast<std::uint16_t>(std::stoul(tls_port));
    options.tls = true;
    options.ca_file = ca_file;
    std::exception_ptr failure;
    auto work = [&]() -> cn::task<void>
    {
        try
        {
            checked(co_await client.connect(options));
            auto result = checked(co_await client.query("SELECT toUInt8(7)"));
            ASSERT_EQ(std::get<std::uint64_t>(result.blocks.back().columns[0].values[0].data), std::uint64_t{7});
            client.close();
            options.host = "localhost"; // CA is trusted, but SAN only contains 127.0.0.1.
            auto wrong_name = co_await client.connect(options);
            ASSERT_FALSE(wrong_name.has_value());
            ASSERT_FALSE(client.is_open());
            options.host = "127.0.0.1";
            options.ca_file.clear();
            auto untrusted = co_await client.connect(options);
            ASSERT_FALSE(untrusted.has_value());
            ASSERT_FALSE(client.is_open());
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        client.close();
        io->stop();
    };
    cn::spawn(*io, work());
    io->run();
    if (failure)
        std::rethrow_exception(failure);
}


#endif

int main()
{
    if (!std::getenv("CNETMOD_CLICKHOUSE_TEST_PORT"))
        return 77;
    return cn::test::run_all();
}
