#include "test_framework.hpp"
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;
using namespace cnetmod::mongodb;

TEST(mongodb_srv_uri_validation_precedes_dns)
{
    for (const auto uri : {"mongodb+srv://cluster.example.com:27017/", "mongodb+srv://a.example.com,b.example.com/",
             "mongodb+srv://127.0.0.1/", "mongodb://host/?srvMaxHosts=2", "mongodb://host:/", "mongodb://host/?tls=maybe"})
        ASSERT_FALSE(parse_connection_string(uri).has_value());
    auto config = parse_connection_string("mongodb+srv://user:p%40ss@cluster.EXAMPLE.com/db?authSource=admin&tls=false&compressors=snappy,zstd");
    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(*config->srv_hostname, std::string{"cluster.example.com"});
    ASSERT_EQ(config->topology.per_server_pool.connection.password, std::string{"p@ss"});
    ASSERT_FALSE(config->topology.per_server_pool.connection.tls);
    ASSERT_TRUE(config->topology.per_server_pool.connection.enable_snappy_compression);
    ASSERT_EQ(config->topology.per_server_pool.connection.compression_preference.front(), compressor_snappy);
    ASSERT_EQ(config->topology.per_server_pool.connection.compression_preference.back(), compressor_zstd);
    ASSERT_FALSE(parse_connection_string("mongodb://host/?tls=true&ssl=false").has_value());
    ASSERT_FALSE(parse_connection_string("mongodb://host/?compressors=snappy,").has_value());
    ASSERT_TRUE(parse_connection_string("mongodb+srv://cluster.example.com/")->topology.per_server_pool.connection.tls);
    ASSERT_FALSE(parse_connection_string("mongodb://key:secret@host/?authMechanism=MONGODB-AWS").has_value());
    ASSERT_FALSE(parse_connection_string("mongodb://host/?authSource=one&AUTHSOURCE=two").has_value());
}

TEST(mongodb_srv_validates_all_targets_and_normalizes_idna)
{
    srv_dns_records records{{{{"NODE.EXAMPLE.COM.", 27017}, std::chrono::seconds{5}}}, {"authSource=admin"}};
    auto valid = validate_srv_seedlist("cluster.example.com", records);
    ASSERT_TRUE(valid.has_value());
    ASSERT_EQ(valid->seeds.front().host, std::string{"node.example.com"});
    ASSERT_EQ(valid->ttl.count(), 60);
    records.servers.push_back({{"node.evil-example.com", 27017}, std::chrono::seconds{60}});
    ASSERT_FALSE(validate_srv_seedlist("cluster.example.com", records, 1).has_value());
    ASSERT_FALSE(validate_srv_seedlist("example.com", {{{{"example.com", 27017}, std::chrono::seconds{60}}}, {}}).has_value());
    auto idna = normalize_dns_hostname("b\xc3\xbc" "cher.example");
    ASSERT_TRUE(idna.has_value());
    ASSERT_EQ(*idna, std::string{"xn--bcher-kva.example"});
    ASSERT_FALSE(validate_srv_seedlist("cluster.example.com", {{{{"node.example.com", 27017}, std::chrono::seconds{60}}}, {"a=1", "b=2"}}).has_value());
}

TEST(mongodb_srv_txt_defaults_precedence_and_poll_callback)
{
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    unsigned calls{};
    srv_lookup_callback lookup = [&](std::string hostname, std::string service) -> cnetmod::task<result<srv_dns_records>>
    {
        ASSERT_EQ(hostname, std::string{"cluster.example.com"});
        ASSERT_EQ(service, std::string{"mongodb"});
        ++calls;
        co_return srv_dns_records{{{{"node.example.com", 27017}, std::chrono::seconds{120}}}, {"authSource=txt&replicaSet=rs"}};
    };
    auto task = [&]() -> cnetmod::task<void>
    {
        auto config = co_await resolve_connection_string(*io, pool, "mongodb+srv://cluster.example.com/?authSource=uri", lookup);
        ASSERT_TRUE(config.has_value());
        ASSERT_EQ(config->topology.per_server_pool.connection.authentication_database, std::string{"uri"});
        ASSERT_EQ(*config->topology.replica_set_name, std::string{"rs"});
        ASSERT_TRUE(static_cast<bool>(config->topology.refresh_srv));
        auto refreshed = co_await config->topology.refresh_srv(config->topology.seeds);
        ASSERT_TRUE(refreshed.has_value());
        ASSERT_EQ(calls, 2U);
        auto conflict = co_await resolve_connection_string(*io, pool, "mongodb+srv://cluster.example.com/?srvMaxHosts=1", lookup);
        ASSERT_FALSE(conflict.has_value());
        srv_lookup_callback malicious = [](std::string, std::string) -> cnetmod::task<result<srv_dns_records>>
        {
            co_return srv_dns_records{{{{"node.example.com", 27017}, std::chrono::seconds{60}}}, {"tls=false"}};
        };
        ASSERT_FALSE((co_await resolve_connection_string(*io, pool, "mongodb+srv://cluster.example.com/", malicious)).has_value());
        srv_lookup_callback throwing = [](std::string, std::string) -> cnetmod::task<result<srv_dns_records>>
        {
            throw std::runtime_error("resolver unavailable");
            co_return srv_dns_records{};
        };
        ASSERT_FALSE((co_await resolve_connection_string(*io, pool, "mongodb+srv://cluster.example.com/", throwing)).has_value());
        io->stop();
    };
    cnetmod::spawn(*io, task());
    io->run();
}
RUN_TESTS()
